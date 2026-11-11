#include "http_server/http_server.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/text_utf8.h"
#include "cJSON.h"
#include "data/data_validate.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "font/pp_font.h"
#include "image/image_meta.h"
#include "image/image_store.h"
#include "storage/storage_budget.h"
#include "web/web_assets.h"

static const char *TAG = "http";

#define JSON_BODY_MAX (48u * 1024u)
#define PROFILE_BODY_MAX (16u * 1024u)
#define CLAIM_BODY_MAX (1024u)
#define VALIDATE_BODY_MAX (8u * 1024u)
#define UPLOAD_CHUNK 1024u

static const pp_http_context_t *s_ctx;
static httpd_handle_t s_server;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void lock_session(void)
{
    if (s_ctx && s_ctx->session_lock) {
        xSemaphoreTake(s_ctx->session_lock, portMAX_DELAY);
    }
}

static void unlock_session(void)
{
    if (s_ctx && s_ctx->session_lock) xSemaphoreGive(s_ctx->session_lock);
}

static const char *status_text(int code)
{
    switch (code) {
    case 200: return "200 OK";
    case 204: return "204 No Content";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 411: return "411 Length Required";
    case 413: return "413 Payload Too Large";
    case 415: return "415 Unsupported Media Type";
    case 500: return "500 Internal Server Error";
    case 507: return "507 Insufficient Storage";
    default: return "500 Internal Server Error";
    }
}

static void set_json_headers(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
}

static esp_err_t send_cjson(httpd_req_t *req, int code, cJSON *root)
{
    char *body = root ? cJSON_PrintUnformatted(root) : NULL;
    if (root) cJSON_Delete(root);
    if (!body) {
        httpd_resp_set_status(req, status_text(500));
        set_json_headers(req);
        return httpd_resp_send(req, "{\"error\":\"internal\"}",
                              HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_status(req, status_text(code));
    set_json_headers(req);
    const esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return err;
}

static esp_err_t send_json_text(httpd_req_t *req, int code, char *text)
{
    if (!text) return send_cjson(req, 500, NULL);
    httpd_resp_set_status(req, status_text(code));
    set_json_headers(req);
    const esp_err_t err = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return err;
}

static esp_err_t send_error(httpd_req_t *req, int code, const char *slug,
                            const char *message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return send_cjson(req, 500, NULL);
    cJSON_AddStringToObject(root, "error", slug ? slug : "error");
    cJSON_AddStringToObject(root, "message", message ? message : "");
    return send_cjson(req, code, root);
}

static esp_err_t recv_exact(httpd_req_t *req, uint8_t *buffer, size_t length)
{
    size_t received = 0;
    int timeouts = 0;
    while (received < length) {
        const int result = httpd_req_recv(req, (char *)buffer + received,
                                          length - received);
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > 3) return ESP_FAIL;
            continue;
        }
        if (result <= 0) return ESP_FAIL;
        received += (size_t)result;
        timeouts = 0;
    }
    return ESP_OK;
}

static esp_err_t read_body(httpd_req_t *req, char **out, size_t *out_len,
                           size_t max_length, int *http_status)
{
    *out = NULL;
    *out_len = 0;
    char length_text[16];
    if (httpd_req_get_hdr_value_str(req, "Content-Length", length_text,
                                    sizeof(length_text)) != ESP_OK) {
        *http_status = 411;
        return ESP_FAIL;
    }
    char *end = NULL;
    const unsigned long length = strtoul(length_text, &end, 10);
    if (!end || *end != '\0' || length == 0 || length > max_length) {
        *http_status = length > max_length ? 413 : 400;
        return ESP_FAIL;
    }
    char *body = malloc((size_t)length + 1);
    if (!body) {
        *http_status = 500;
        return ESP_FAIL;
    }
    if (recv_exact(req, (uint8_t *)body, (size_t)length) != ESP_OK) {
        free(body);
        *http_status = 400;
        return ESP_FAIL;
    }
    body[length] = '\0';
    *out = body;
    *out_len = (size_t)length;
    return ESP_OK;
}

static bool get_header(httpd_req_t *req, const char *name, char *out,
                       size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    const size_t length = httpd_req_get_hdr_value_len(req, name);
    if (length == 0 || length >= out_size) return false;
    return httpd_req_get_hdr_value_str(req, name, out, out_size) == ESP_OK;
}

static bool authorize(httpd_req_t *req, char *token_out, size_t token_size)
{
    char token[PP_MGMT_TOKEN_MAX + 1];
    if (!get_header(req, "X-PP-Token", token, sizeof(token))) return false;
    if (!s_ctx || !s_ctx->session) return false;
    lock_session();
    const bool ok = pp_mgmt_session_heartbeat(s_ctx->session, now_ms(), token);
    unlock_session();
    if (ok && token_out && token_size > 0) {
        snprintf(token_out, token_size, "%s", token);
    }
    return ok;
}

static void query_client(httpd_req_t *req, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    const size_t length = httpd_req_get_url_query_len(req);
    if (length == 0 || length > 256) return;
    char query[257];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return;
    (void)httpd_query_key_value(query, "client", out, out_size);
}

static void make_token(char *out, size_t out_size)
{
    snprintf(out, out_size, "%08lx%08lx", (unsigned long)esp_random(),
             (unsigned long)esp_random());
}

static bool storage_numbers(uint64_t *used, uint64_t *total, int *percent,
                            int *level)
{
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
    if (!s_ctx || pp_storage_usage(s_ctx->storage, &used_bytes, &total_bytes) !=
                      PP_STORAGE_OK) {
        return false;
    }
    if (used) *used = used_bytes;
    if (total) *total = total_bytes;
    if (percent) *percent = pp_storage_percent(used_bytes, total_bytes);
    if (level) *level = (int)pp_storage_level(used_bytes, total_bytes);
    return true;
}

static esp_err_t storage_failure(httpd_req_t *req, pp_storage_err_t error)
{
    const int code = error == PP_STORAGE_ERR_NO_SPACE ? 507 : 500;
    return send_error(req, code, pp_storage_error_name(error),
                      error == PP_STORAGE_ERR_NO_SPACE
                          ? "设备存储空间不足，请先删除不需要的图片"
                          : "存储写入失败");
}

static bool check_font_field(const char *field, const char *text, char *error,
                             size_t error_size)
{
    uint32_t missing = 0;
    if (!pp_font_first_missing(text ? text : "", &missing)) return true;
    pp_set_error(error, error_size, "%s 含有字体不支持的字符 U+%04lX", field,
                 (unsigned long)missing);
    return false;
}

static bool profile_font_check(const pp_profile_t *profile, char *error,
                               size_t error_size)
{
    if (!check_font_field("姓名", profile->name, error, error_size) ||
        !check_font_field("一句话介绍", profile->tagline, error, error_size) ||
        !check_font_field("Steam ID", profile->steam_id, error, error_size) ||
        !check_font_field("Switch ID", profile->switch_id, error, error_size) ||
        !check_font_field("PlayStation ID", profile->psn_id, error,
                          error_size)) {
        return false;
    }
    for (size_t i = 0; i < profile->contact_count; ++i) {
        char label[40];
        snprintf(label, sizeof(label), "联系方式%u平台", (unsigned)i + 1);
        if (!check_font_field(label, profile->contacts[i].platform, error,
                              error_size)) {
            return false;
        }
        snprintf(label, sizeof(label), "联系方式%u账号", (unsigned)i + 1);
        if (!check_font_field(label, profile->contacts[i].account, error,
                              error_size)) {
            return false;
        }
        snprintf(label, sizeof(label), "联系方式%u链接", (unsigned)i + 1);
        if (!check_font_field(label, profile->contacts[i].link, error,
                              error_size)) {
            return false;
        }
    }
    return true;
}

/* ---------------------------- static assets ----------------------------- */

static esp_err_t send_gzip_asset(httpd_req_t *req, const uint8_t *data,
                                 size_t length, const char *content_type)
{
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)data, length);
}

static esp_err_t handler_index(httpd_req_t *req)
{
    return send_gzip_asset(req, pp_web_index_gz, pp_web_index_gz_len,
                           "text/html; charset=utf-8");
}

static esp_err_t handler_css(httpd_req_t *req)
{
    return send_gzip_asset(req, pp_web_css_gz, pp_web_css_gz_len,
                           "text/css; charset=utf-8");
}

static esp_err_t handler_js(httpd_req_t *req)
{
    return send_gzip_asset(req, pp_web_js_gz, pp_web_js_gz_len,
                           "application/javascript; charset=utf-8");
}

static esp_err_t handler_favicon(httpd_req_t *req)
{
    httpd_resp_set_status(req, status_text(204));
    return httpd_resp_send(req, NULL, 0);
}

/* ------------------------------- status --------------------------------- */

static esp_err_t handler_status(httpd_req_t *req)
{
    if (!s_ctx) return send_error(req, 500, "internal", "server not ready");
    char client_id[PP_MGMT_CLIENT_ID_MAX + 1];
    query_client(req, client_id, sizeof(client_id));
    char token[PP_MGMT_TOKEN_MAX + 1] = { 0 };
    (void)get_header(req, "X-PP-Token", token, sizeof(token));

    pp_mgmt_status_t session_status;
    lock_session();
    pp_mgmt_session_status(s_ctx->session, now_ms(), client_id, token,
                           &session_status);
    unlock_session();

    uint64_t used = 0;
    uint64_t total = 0;
    int percent = 0;
    int level = 0;
    (void)storage_numbers(&used, &total, &percent, &level);

    cJSON *root = cJSON_CreateObject();
    if (!root) return send_error(req, 500, "internal", "内存不足");
    const esp_app_desc_t *desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware", desc ? desc->version : "unknown");
    cJSON_AddNumberToObject(root, "uptime_ms", (double)now_ms());

    cJSON *session = cJSON_AddObjectToObject(root, "session");
    if (session) {
        cJSON_AddBoolToObject(session, "running", session_status.running);
        cJSON_AddBoolToObject(session, "occupied", session_status.occupied);
        cJSON_AddBoolToObject(session, "editor", session_status.is_editor);
        cJSON_AddBoolToObject(session, "reserved_for_me",
                              session_status.reserved_for_me);
        cJSON_AddStringToObject(session, "client_name",
                                session_status.client_name);
        cJSON_AddNumberToObject(session, "grace_remaining_s",
                                session_status.grace_remaining_s);
        cJSON_AddNumberToObject(session, "idle_remaining_s",
                                session_status.idle_remaining_s);
    }

    cJSON *storage = cJSON_AddObjectToObject(root, "storage");
    if (storage) {
        cJSON_AddNumberToObject(storage, "used", (double)used);
        cJSON_AddNumberToObject(storage, "total", (double)total);
        cJSON_AddNumberToObject(storage, "free",
                                (double)(total > used ? total - used : 0));
        cJSON_AddNumberToObject(storage, "percent", percent);
        cJSON_AddNumberToObject(storage, "level", level);
        cJSON_AddNumberToObject(storage, "warn_percent",
                                PP_STORAGE_WARN_PERCENT);
        cJSON_AddNumberToObject(storage, "critical_percent",
                                PP_STORAGE_CRITICAL_PERCENT);
    }

    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    if (wifi) {
        cJSON_AddStringToObject(wifi, "ssid",
                                s_ctx->ap_ssid ? s_ctx->ap_ssid : "");
        cJSON_AddStringToObject(wifi, "ip", s_ctx->ap_ip ? s_ctx->ap_ip : "");
        cJSON_AddNumberToObject(wifi, "clients",
                                s_ctx->client_count ? s_ctx->client_count() : 0);
    }
    return send_cjson(req, 200, root);
}

/* ------------------------------- session -------------------------------- */

static esp_err_t handler_claim(httpd_req_t *req)
{
    char *body = NULL;
    size_t length = 0;
    int code = 400;
    if (read_body(req, &body, &length, CLAIM_BODY_MAX, &code) != ESP_OK) {
        return send_error(req, code, "body", "请求内容无效");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(req, 400, "json", "请求不是有效 JSON");
    }
    const cJSON *client =
        cJSON_GetObjectItemCaseSensitive(root, "client_id");
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "client_name");
    if (!cJSON_IsString(client) || !pp_text_is_ascii_id(
                                      client->valuestring, 4,
                                      PP_MGMT_CLIENT_ID_MAX)) {
        cJSON_Delete(root);
        return send_error(req, 400, "client_id", "客户端标识无效");
    }
    const char *client_name =
        cJSON_IsString(name) ? name->valuestring : "浏览器";
    char client_copy[PP_MGMT_CLIENT_ID_MAX + 1];
    char name_copy[PP_MGMT_CLIENT_NAME_MAX + 1];
    snprintf(client_copy, sizeof(client_copy), "%s", client->valuestring);
    snprintf(name_copy, sizeof(name_copy), "%s", client_name);
    cJSON_Delete(root);

    char token[PP_MGMT_TOKEN_MAX + 1];
    make_token(token, sizeof(token));

    pp_mgmt_claim_result_t result;
    pp_mgmt_status_t status;
    lock_session();
    result = pp_mgmt_session_claim(s_ctx->session, now_ms(), client_copy,
                                   name_copy, token, NULL, 0);
    pp_mgmt_session_status(s_ctx->session, now_ms(), client_copy, token,
                           &status);
    unlock_session();

    if (result == PP_MGMT_CLAIM_OK) {
        cJSON *reply = cJSON_CreateObject();
        if (!reply) return send_error(req, 500, "internal", "内存不足");
        cJSON_AddBoolToObject(reply, "ok", true);
        cJSON_AddStringToObject(reply, "token", token);
        cJSON_AddNumberToObject(reply, "lease_seconds",
                                PP_MGMT_LEASE_TIMEOUT_MS / 1000);
        cJSON_AddNumberToObject(reply, "grace_seconds",
                                PP_MGMT_GRACE_MS / 1000);
        cJSON_AddNumberToObject(reply, "idle_seconds",
                                status.idle_remaining_s);
        return send_cjson(req, 200, reply);
    }
    if (result == PP_MGMT_CLAIM_OCCUPIED) {
        cJSON *reply = cJSON_CreateObject();
        if (!reply) return send_error(req, 500, "internal", "内存不足");
        cJSON_AddStringToObject(reply, "error", "occupied");
        cJSON_AddStringToObject(reply, "message",
                                "设备正在被另一个浏览器编辑");
        cJSON_AddStringToObject(reply, "client_name", status.client_name);
        cJSON_AddNumberToObject(reply, "grace_remaining_s",
                                status.grace_remaining_s);
        cJSON_AddBoolToObject(reply, "reserved_for_me",
                              status.reserved_for_me);
        return send_cjson(req, 409, reply);
    }
    return send_error(req, 400, "claim", "无法取得编辑权限");
}

static esp_err_t handler_heartbeat(httpd_req_t *req)
{
    char token[PP_MGMT_TOKEN_MAX + 1];
    if (!authorize(req, token, sizeof(token))) {
        return send_error(req, 401, "lease", "编辑权限已失效");
    }
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    lock_session();
    const int idle = pp_mgmt_session_idle_remaining_s(s_ctx->session, now_ms());
    unlock_session();
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddNumberToObject(reply, "idle_remaining_s", idle);
    return send_cjson(req, 200, reply);
}

static esp_err_t handler_release(httpd_req_t *req)
{
    char token[PP_MGMT_TOKEN_MAX + 1];
    if (!get_header(req, "X-PP-Token", token, sizeof(token))) {
        return send_error(req, 401, "lease", "缺少编辑令牌");
    }
    lock_session();
    const bool ok = pp_mgmt_session_release(s_ctx->session, now_ms(), token);
    unlock_session();
    if (!ok) return send_error(req, 401, "lease", "编辑权限已失效");
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    cJSON_AddBoolToObject(reply, "ok", true);
    return send_cjson(req, 200, reply);
}

/* ------------------------------- profile -------------------------------- */

static esp_err_t handler_profile_get(httpd_req_t *req)
{
    if (!s_ctx) return send_error(req, 500, "internal", "server not ready");
    (void)pp_image_profile_flags(s_ctx->storage, s_ctx->images);
    char *json = pp_profile_to_json(s_ctx->profile, s_ctx->images);
    if (!json) return send_error(req, 500, "internal", "内存不足");
    return send_json_text(req, 200, json);
}

static esp_err_t handler_profile_put(httpd_req_t *req)
{
    if (!authorize(req, NULL, 0)) {
        return send_error(req, 401, "lease", "编辑权限已失效");
    }
    char *body = NULL;
    size_t length = 0;
    int code = 400;
    if (read_body(req, &body, &length, PROFILE_BODY_MAX, &code) != ESP_OK) {
        return send_error(req, code, "body", "请求内容无效");
    }
    pp_profile_t *parsed = calloc(1, sizeof(*parsed));
    if (!parsed) {
        free(body);
        return send_error(req, 500, "internal", "内存不足");
    }
    char error[160];
    const bool ok = pp_profile_from_json(body, parsed, error, sizeof(error));
    free(body);
    if (!ok) {
        free(parsed);
        return send_error(req, 400, "profile", error);
    }
    if (!profile_font_check(parsed, error, sizeof(error))) {
        free(parsed);
        return send_error(req, 400, "font", error);
    }
    char *file_json = pp_profile_to_json(parsed, NULL);
    if (!file_json) {
        free(parsed);
        return send_error(req, 500, "internal", "内存不足");
    }
    const pp_storage_err_t write = pp_storage_write_atomic(
        s_ctx->storage, "data/profile.json", file_json, strlen(file_json));
    cJSON_free(file_json);
    if (write != PP_STORAGE_OK) {
        free(parsed);
        return storage_failure(req, write);
    }

    *s_ctx->profile = *parsed;
    free(parsed);
    (void)pp_image_profile_flags(s_ctx->storage, s_ctx->images);
    char *reply = pp_profile_to_json(s_ctx->profile, s_ctx->images);
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    return send_json_text(req, 200, reply);
}

/* -------------------------------- games --------------------------------- */

static bool games_font_check(const pp_games_t *games, char *error,
                             size_t error_size)
{
    for (size_t i = 0; i < games->count; ++i) {
        if (!check_font_field("游戏名称", games->items[i].title, error,
                              error_size) ||
            !check_font_field("游戏介绍", games->items[i].note, error,
                              error_size) ||
            !check_font_field("游戏链接", games->items[i].link, error,
                              error_size)) {
            return false;
        }
    }
    return true;
}

static void prune_game_covers(const pp_games_t *games)
{
    if (!s_ctx) return;
    char dir_path[PP_STORAGE_PATH_MAX + 128];
    if (!pp_storage_abs_path(s_ctx->storage, "img/games", dir_path,
                             sizeof(dir_path))) {
        return;
    }
    DIR *dir = opendir(dir_path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        const size_t length = strlen(name);
        if (length < 9 || strcmp(name + length - 4, ".bin") != 0) continue;
        char id[PP_GAME_ID_MAX + 1];
        const size_t id_length = length - 4;
        if (id_length > PP_GAME_ID_MAX) continue;
        memcpy(id, name, id_length);
        id[id_length] = '\0';
        if (!pp_game_id_valid(id)) continue;
        if (pp_games_find_const(games, id)) continue;
        char rel[PP_STORAGE_PATH_MAX];
        if (pp_image_cover_path(id, rel, sizeof(rel))) {
            (void)pp_storage_remove(s_ctx->storage, rel);
            ESP_LOGI(TAG, "清理无主封面: %s", id);
        }
    }
    closedir(dir);
}

static esp_err_t handler_games_get(httpd_req_t *req)
{
    if (!s_ctx) return send_error(req, 500, "internal", "server not ready");
    (void)pp_image_game_flags(s_ctx->storage, s_ctx->games);
    char *json = pp_games_to_json(s_ctx->games, true);
    if (!json) return send_error(req, 500, "internal", "内存不足");
    return send_json_text(req, 200, json);
}

static esp_err_t handler_games_put(httpd_req_t *req)
{
    if (!authorize(req, NULL, 0)) {
        return send_error(req, 401, "lease", "编辑权限已失效");
    }
    char *body = NULL;
    size_t length = 0;
    int code = 400;
    if (read_body(req, &body, &length, JSON_BODY_MAX, &code) != ESP_OK) {
        return send_error(req, code, "body", "请求内容无效");
    }
    pp_games_t parsed;
    pp_games_init(&parsed);
    char error[160];
    const bool ok = pp_games_from_json(body, &parsed, error, sizeof(error));
    free(body);
    if (!ok) {
        pp_games_free(&parsed);
        return send_error(req, 400, "games", error);
    }
    if (!games_font_check(&parsed, error, sizeof(error))) {
        pp_games_free(&parsed);
        return send_error(req, 400, "font", error);
    }
    char *file_json = pp_games_to_json(&parsed, false);
    if (!file_json) {
        pp_games_free(&parsed);
        return send_error(req, 500, "internal", "内存不足");
    }
    const pp_storage_err_t write = pp_storage_write_atomic(
        s_ctx->storage, "data/games.json", file_json, strlen(file_json));
    cJSON_free(file_json);
    if (write != PP_STORAGE_OK) {
        pp_games_free(&parsed);
        return storage_failure(req, write);
    }

    pp_games_t old = *s_ctx->games;
    *s_ctx->games = parsed;
    prune_game_covers(s_ctx->games);
    pp_games_free(&old);
    (void)pp_image_game_flags(s_ctx->storage, s_ctx->games);
    char *reply = pp_games_to_json(s_ctx->games, true);
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    return send_json_text(req, 200, reply);
}

/* --------------------------------- nfc ---------------------------------- */

static esp_err_t handler_nfc_get(httpd_req_t *req)
{
    if (!s_ctx) return send_error(req, 500, "internal", "server not ready");
    char *json = pp_nfc_to_json(s_ctx->nfc);
    if (!json) return send_error(req, 500, "internal", "内存不足");
    return send_json_text(req, 200, json);
}

static esp_err_t handler_nfc_put(httpd_req_t *req)
{
    if (!authorize(req, NULL, 0)) {
        return send_error(req, 401, "lease", "编辑权限已失效");
    }
    char *body = NULL;
    size_t length = 0;
    int code = 400;
    if (read_body(req, &body, &length, 4096, &code) != ESP_OK) {
        return send_error(req, code, "body", "请求内容无效");
    }
    pp_nfc_t parsed;
    char error[160];
    const bool ok = pp_nfc_from_json(body, &parsed, error, sizeof(error));
    free(body);
    if (!ok) return send_error(req, 400, "nfc", error);
    if (!check_font_field("NFC 标题", parsed.label, error, sizeof(error)) ||
        !check_font_field("NFC 内容", parsed.content, error, sizeof(error))) {
        return send_error(req, 400, "font", error);
    }
    char *file_json = pp_nfc_to_json(&parsed);
    if (!file_json) return send_error(req, 500, "internal", "内存不足");
    const pp_storage_err_t write = pp_storage_write_atomic(
        s_ctx->storage, "data/nfc.json", file_json, strlen(file_json));
    cJSON_free(file_json);
    if (write != PP_STORAGE_OK) return storage_failure(req, write);
    *s_ctx->nfc = parsed;
    char *reply = pp_nfc_to_json(s_ctx->nfc);
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    return send_json_text(req, 200, reply);
}

/* -------------------------------- images -------------------------------- */

static const char *slot_from_uri(httpd_req_t *req)
{
    static const char prefix[] = "/api/images/";
    if (strncmp(req->uri, prefix, sizeof(prefix) - 1) != 0) return NULL;
    const char *slot = req->uri + sizeof(prefix) - 1;
    return slot[0] ? slot : NULL;
}

static esp_err_t handler_image_get(httpd_req_t *req)
{
    const char *slot_name = slot_from_uri(req);
    pp_image_slot_info_t slot;
    if (!slot_name || !pp_image_slot_parse(slot_name, &slot)) {
        return send_error(req, 400, "slot", "图片槽位无效");
    }
    char rel[PP_STORAGE_PATH_MAX];
    char path[PP_STORAGE_PATH_MAX + 128];
    if (!pp_image_slot_path(&slot, rel, sizeof(rel)) ||
        !pp_storage_abs_path(s_ctx->storage, rel, path, sizeof(path))) {
        return send_error(req, 400, "slot", "图片槽位无效");
    }
    FILE *file = fopen(path, "rb");
    if (!file) return send_error(req, 404, "not_found", "图片不存在");
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    uint8_t buffer[UPLOAD_CHUNK];
    size_t read_bytes;
    esp_err_t err = ESP_OK;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        err = httpd_resp_send_chunk(req, (const char *)buffer, read_bytes);
        if (err != ESP_OK) break;
    }
    fclose(file);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

static esp_err_t image_upload(httpd_req_t *req, const pp_image_slot_info_t *slot,
                              const char *rel, const char *token)
{
    const size_t expected = pp_image_kind_file_bytes(slot->kind);
    if (expected == 0) return send_error(req, 400, "slot", "图片槽位无效");

    char type[64];
    if (!get_header(req, "Content-Type", type, sizeof(type)) ||
        strncmp(type, "application/octet-stream", 24) != 0) {
        return send_error(req, 415, "content_type",
                          "请以 application/octet-stream 上传图片");
    }
    char length_text[16];
    if (httpd_req_get_hdr_value_str(req, "Content-Length", length_text,
                                    sizeof(length_text)) != ESP_OK) {
        return send_error(req, 411, "length", "缺少 Content-Length");
    }
    char *end = NULL;
    const unsigned long declared = strtoul(length_text, &end, 10);
    if (!end || *end != '\0' || declared != expected) {
        return send_error(req, 400, "length",
                          "图片尺寸/字节数不符合该槽位要求");
    }

    uint64_t used = 0;
    uint64_t total = 0;
    if (storage_numbers(&used, &total, NULL, NULL)) {
        const uint64_t free_bytes = total > used ? total - used : 0;
        if (!pp_storage_can_fit(free_bytes, expected,
                                pp_storage_reserve_bytes())) {
            return send_error(req, 507, "no_space",
                              "设备存储空间不足，请先删除不需要的图片");
        }
    }

    bool long_started = false;
    lock_session();
    long_started = pp_mgmt_session_begin_long_op(s_ctx->session, token);
    unlock_session();

    pp_storage_temp_t temp;
    pp_storage_err_t storage_error = pp_storage_begin_temp(s_ctx->storage, rel,
                                                           &temp);
    if (storage_error != PP_STORAGE_OK) {
        if (long_started) {
            lock_session();
            (void)pp_mgmt_session_end_long_op(s_ctx->session, token);
            unlock_session();
        }
        return storage_failure(req, storage_error);
    }

    esp_err_t result = ESP_OK;
    int http_code = 200;
    const char *error_slug = NULL;
    const char *error_text = NULL;
    uint8_t header[PP_IMAGE_HEADER_BYTES];
    uint8_t buffer[UPLOAD_CHUNK];
    pp_image_meta_t meta;

    if (recv_exact(req, header, sizeof(header)) != ESP_OK) {
        result = ESP_FAIL;
        http_code = 400;
        error_slug = "body";
        error_text = "图片头部接收失败";
        goto done;
    }
    if (pp_image_parse_header(header, sizeof(header), &meta) != PP_IMAGE_OK) {
        result = ESP_FAIL;
        http_code = 400;
        error_slug = "header";
        error_text = "图片头部无效";
        goto done;
    }
    uint16_t want_w = 0;
    uint16_t want_h = 0;
    (void)pp_image_kind_dimensions(slot->kind, &want_w, &want_h);
    if (meta.width != want_w || meta.height != want_h ||
        meta.data_len != expected - PP_IMAGE_HEADER_BYTES) {
        result = ESP_FAIL;
        http_code = 400;
        error_slug = "dimensions";
        error_text = "图片尺寸不符合槽位要求";
        goto done;
    }
    storage_error = pp_storage_write_temp(&temp, header, sizeof(header));
    if (storage_error != PP_STORAGE_OK) {
        result = ESP_FAIL;
        http_code = storage_error == PP_STORAGE_ERR_NO_SPACE ? 507 : 500;
        error_slug = pp_storage_error_name(storage_error);
        error_text = "图片写入失败";
        goto done;
    }

    uint32_t crc = 0;
    size_t remaining = meta.data_len;
    while (remaining > 0) {
        const size_t chunk = remaining < sizeof(buffer) ? remaining
                                                        : sizeof(buffer);
        if (recv_exact(req, buffer, chunk) != ESP_OK) {
            result = ESP_FAIL;
            http_code = 400;
            error_slug = "body";
            error_text = "图片数据传输中断";
            goto done;
        }
        crc = pp_crc32_update(crc, buffer, chunk);
        storage_error = pp_storage_write_temp(&temp, buffer, chunk);
        if (storage_error != PP_STORAGE_OK) {
            result = ESP_FAIL;
            http_code = storage_error == PP_STORAGE_ERR_NO_SPACE ? 507 : 500;
            error_slug = pp_storage_error_name(storage_error);
            error_text = "图片写入失败";
            goto done;
        }
        remaining -= chunk;
    }
    if (crc != meta.crc32) {
        result = ESP_FAIL;
        http_code = 400;
        error_slug = "crc";
        error_text = "图片校验失败，请重新上传";
        goto done;
    }
    storage_error = pp_storage_commit_temp(s_ctx->storage, &temp);
    if (storage_error != PP_STORAGE_OK) {
        result = ESP_FAIL;
        http_code = storage_error == PP_STORAGE_ERR_NO_SPACE ? 507 : 500;
        error_slug = pp_storage_error_name(storage_error);
        error_text = "图片保存失败";
        goto done;
    }
    temp.open = false;

    if (slot->slot == PP_IMAGE_SLOT_GAME_COVER) {
        pp_game_t *game = pp_games_find(s_ctx->games, slot->game_id);
        if (game) game->has_cover = true;
    } else {
        (void)pp_image_profile_flags(s_ctx->storage, s_ctx->images);
    }

done:
    if (temp.open) pp_storage_abort_temp(s_ctx->storage, &temp);
    if (long_started) {
        lock_session();
        (void)pp_mgmt_session_end_long_op(s_ctx->session, token);
        unlock_session();
    }
    if (result != ESP_OK) {
        return send_error(req, http_code, error_slug ? error_slug : "upload",
                          error_text ? error_text : "上传失败");
    }

    uint64_t used2 = 0;
    uint64_t total2 = 0;
    int percent = 0;
    int level = 0;
    (void)storage_numbers(&used2, &total2, &percent, &level);
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddNumberToObject(reply, "percent", percent);
    cJSON_AddNumberToObject(reply, "level", level);
    cJSON_AddNumberToObject(reply, "used", (double)used2);
    cJSON_AddNumberToObject(reply, "total", (double)total2);
    return send_cjson(req, 200, reply);
}

static esp_err_t handler_image_put(httpd_req_t *req)
{
    const char *slot_name = slot_from_uri(req);
    pp_image_slot_info_t slot;
    if (!slot_name || !pp_image_slot_parse(slot_name, &slot)) {
        return send_error(req, 400, "slot", "图片槽位无效");
    }
    char token[PP_MGMT_TOKEN_MAX + 1];
    if (!authorize(req, token, sizeof(token))) {
        return send_error(req, 401, "lease", "编辑权限已失效");
    }
    char rel[PP_STORAGE_PATH_MAX];
    if (!pp_image_slot_path(&slot, rel, sizeof(rel))) {
        return send_error(req, 400, "slot", "图片槽位无效");
    }
    return image_upload(req, &slot, rel, token);
}

static esp_err_t handler_image_delete(httpd_req_t *req)
{
    const char *slot_name = slot_from_uri(req);
    pp_image_slot_info_t slot;
    if (!slot_name || !pp_image_slot_parse(slot_name, &slot)) {
        return send_error(req, 400, "slot", "图片槽位无效");
    }
    if (!authorize(req, NULL, 0)) {
        return send_error(req, 401, "lease", "编辑权限已失效");
    }
    char rel[PP_STORAGE_PATH_MAX];
    if (!pp_image_slot_path(&slot, rel, sizeof(rel))) {
        return send_error(req, 400, "slot", "图片槽位无效");
    }
    const pp_storage_err_t removed = pp_storage_remove(s_ctx->storage, rel);
    if (removed != PP_STORAGE_OK && removed != PP_STORAGE_ERR_NOT_FOUND) {
        return storage_failure(req, removed);
    }
    if (slot.slot == PP_IMAGE_SLOT_GAME_COVER) {
        pp_game_t *game = pp_games_find(s_ctx->games, slot.game_id);
        if (game) game->has_cover = false;
    } else {
        (void)pp_image_profile_flags(s_ctx->storage, s_ctx->images);
    }
    int percent = 0;
    int level = 0;
    uint64_t used = 0;
    uint64_t total = 0;
    (void)storage_numbers(&used, &total, &percent, &level);
    cJSON *reply = cJSON_CreateObject();
    if (!reply) return send_error(req, 500, "internal", "内存不足");
    cJSON_AddBoolToObject(reply, "ok", true);
    cJSON_AddNumberToObject(reply, "percent", percent);
    cJSON_AddNumberToObject(reply, "level", level);
    return send_cjson(req, 200, reply);
}

/* ---------------------------- text validation --------------------------- */

static esp_err_t handler_validate_text(httpd_req_t *req)
{
    char *body = NULL;
    size_t length = 0;
    int code = 400;
    if (read_body(req, &body, &length, VALIDATE_BODY_MAX, &code) != ESP_OK) {
        return send_error(req, code, "body", "请求内容无效");
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(req, 400, "json", "请求不是有效 JSON");
    }
    const cJSON *texts = cJSON_GetObjectItemCaseSensitive(root, "texts");
    if (!cJSON_IsArray(texts) || cJSON_GetArraySize(texts) > 8) {
        cJSON_Delete(root);
        return send_error(req, 400, "texts", "texts 必须是最多 8 项的数组");
    }
    cJSON *reply = cJSON_CreateObject();
    if (!reply) {
        cJSON_Delete(root);
        return send_error(req, 500, "internal", "内存不足");
    }
    cJSON *unsupported = cJSON_AddArrayToObject(reply, "unsupported");
    if (!unsupported) {
        cJSON_Delete(root);
        cJSON_Delete(reply);
        return send_error(req, 500, "internal", "内存不足");
    }
    int index = 0;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, texts) {
        if (!cJSON_IsString(item)) {
            cJSON_Delete(root);
            cJSON_Delete(reply);
            return send_error(req, 400, "texts", "texts 只能包含字符串");
        }
        uint32_t missing = 0;
        if (pp_font_first_missing(item->valuestring, &missing)) {
            cJSON *entry = cJSON_CreateObject();
            if (!entry) continue;
            cJSON_AddNumberToObject(entry, "index", index);
            char cp[16];
            snprintf(cp, sizeof(cp), "U+%04lX", (unsigned long)missing);
            cJSON_AddStringToObject(entry, "codepoint", cp);
            cJSON_AddItemToArray(unsupported, entry);
        }
        ++index;
    }
    cJSON_AddBoolToObject(reply, "ok",
                          cJSON_GetArraySize(unsupported) == 0);
    cJSON_Delete(root);
    return send_cjson(req, 200, reply);
}

static esp_err_t handler_api_not_found(httpd_req_t *req)
{
    return send_error(req, 404, "not_found", "接口不存在");
}

/* ------------------------------- start/stop ----------------------------- */

static const httpd_uri_t URIS[] = {
    { .uri = "/", .method = HTTP_GET, .handler = handler_index },
    { .uri = "/index.html", .method = HTTP_GET, .handler = handler_index },
    { .uri = "/style.css", .method = HTTP_GET, .handler = handler_css },
    { .uri = "/app.js", .method = HTTP_GET, .handler = handler_js },
    { .uri = "/favicon.ico", .method = HTTP_GET, .handler = handler_favicon },
    { .uri = "/api/status", .method = HTTP_GET, .handler = handler_status },
    { .uri = "/api/session/claim", .method = HTTP_POST, .handler = handler_claim },
    { .uri = "/api/session/heartbeat", .method = HTTP_POST,
      .handler = handler_heartbeat },
    { .uri = "/api/session/release", .method = HTTP_POST,
      .handler = handler_release },
    { .uri = "/api/profile", .method = HTTP_GET, .handler = handler_profile_get },
    { .uri = "/api/profile", .method = HTTP_PUT, .handler = handler_profile_put },
    { .uri = "/api/games", .method = HTTP_GET, .handler = handler_games_get },
    { .uri = "/api/games", .method = HTTP_PUT, .handler = handler_games_put },
    { .uri = "/api/nfc", .method = HTTP_GET, .handler = handler_nfc_get },
    { .uri = "/api/nfc", .method = HTTP_PUT, .handler = handler_nfc_put },
    { .uri = "/api/images/*", .method = HTTP_GET, .handler = handler_image_get },
    { .uri = "/api/images/*", .method = HTTP_PUT, .handler = handler_image_put },
    { .uri = "/api/images/*", .method = HTTP_DELETE,
      .handler = handler_image_delete },
    { .uri = "/api/validate-text", .method = HTTP_POST,
      .handler = handler_validate_text },
    { .uri = "/api/*", .method = HTTP_GET, .handler = handler_api_not_found },
    { .uri = "/api/*", .method = HTTP_POST, .handler = handler_api_not_found },
    { .uri = "/api/*", .method = HTTP_PUT, .handler = handler_api_not_found },
    { .uri = "/api/*", .method = HTTP_DELETE, .handler = handler_api_not_found },
};

esp_err_t pp_http_server_start(const pp_http_context_t *context)
{
    if (!context || !context->storage || !context->session ||
        !context->session_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_server) return ESP_ERR_INVALID_STATE;
    s_ctx = context;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = sizeof(URIS) / sizeof(URIS[0]);
    config.max_open_sockets = 3;
    config.backlog_conn = 2;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }
    for (size_t i = 0; i < sizeof(URIS) / sizeof(URIS[0]); ++i) {
        err = httpd_register_uri_handler(s_server, &URIS[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "注册 %s 失败: %s", URIS[i].uri,
                     esp_err_to_name(err));
            httpd_stop(s_server);
            s_server = NULL;
            return err;
        }
    }
    ESP_LOGI(TAG, "HTTP 管理服务已启动 (port 80)");
    return ESP_OK;
}

void pp_http_server_stop(void)
{
    if (s_server) {
        (void)httpd_stop(s_server);
        s_server = NULL;
    }
    s_ctx = NULL;
    ESP_LOGI(TAG, "HTTP 管理服务已停止");
}

bool pp_http_server_running(void)
{
    return s_server != NULL;
}
