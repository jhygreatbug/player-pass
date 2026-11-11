#include "data/profile.h"

#include <stdio.h>
#include <string.h>

#include "app/text_utf8.h"
#include "cJSON.h"
#include "data/data_validate.h"

static bool get_string(const cJSON *parent, const char *key, char *dst,
                       size_t dst_size, size_t max_codepoints, bool required,
                       const char *field, char *error, size_t error_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item || cJSON_IsNull(item)) {
        if (required) {
            pp_set_error(error, error_size, "missing field: %s", field);
            return false;
        }
        dst[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item)) {
        pp_set_error(error, error_size, "%s must be a string", field);
        return false;
    }
    const pp_text_result_t result =
        pp_text_copy(dst, dst_size, item->valuestring, max_codepoints);
    if (result == PP_TEXT_INVALID_UTF8) {
        pp_set_error(error, error_size, "%s is not valid UTF-8", field);
        return false;
    }
    if (result == PP_TEXT_TOO_LONG) {
        pp_set_error(error, error_size, "%s exceeds %u characters", field,
                     (unsigned)max_codepoints);
        return false;
    }
    if (result != PP_TEXT_OK) {
        pp_set_error(error, error_size, "%s does not fit its buffer", field);
        return false;
    }
    return true;
}

void pp_profile_defaults(pp_profile_t *profile)
{
    if (!profile) return;
    memset(profile, 0, sizeof(*profile));
}

void pp_profile_images_clear(pp_profile_images_t *images)
{
    if (!images) return;
    memset(images, 0, sizeof(*images));
}

bool pp_profile_validate(const pp_profile_t *profile, char *error,
                         size_t error_size)
{
    if (!profile) {
        pp_set_error(error, error_size, "profile is null");
        return false;
    }
    if (!pp_field_valid(profile->name, PP_PROFILE_NAME_CP, false)) {
        pp_set_error(error, error_size, "name is invalid or too long");
        return false;
    }
    if (!pp_field_valid(profile->tagline, PP_PROFILE_TAGLINE_CP, true)) {
        pp_set_error(error, error_size, "tagline is invalid or too long");
        return false;
    }
    const struct {
        const char *value;
        const char *name;
        size_t limit;
    } fields[] = {
        { profile->steam_id, "steam_id", PP_PROFILE_PLATFORM_ID_CP },
        { profile->switch_id, "switch_id", PP_PROFILE_PLATFORM_ID_CP },
        { profile->psn_id, "psn_id", PP_PROFILE_PLATFORM_ID_CP },
        { profile->weibo_handle, "weibo_handle", PP_PROFILE_WEIBO_HANDLE_CP },
        { profile->wechat_name, "wechat_name", PP_PROFILE_WECHAT_NAME_CP },
        { profile->wechat_id, "wechat_id", PP_PROFILE_WECHAT_ID_CP },
    };
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        if (!pp_field_valid(fields[i].value, fields[i].limit, false)) {
            pp_set_error(error, error_size, "%s is invalid or too long",
                         fields[i].name);
            return false;
        }
    }
    if (!pp_optional_url_valid(profile->weibo_url)) {
        pp_set_error(error, error_size,
                     "weibo_url must be an http(s) URL without spaces");
        return false;
    }
    return true;
}

static void add_string(cJSON *root, const char *key, const char *value)
{
    cJSON_AddStringToObject(root, key, value ? value : "");
}

char *pp_profile_to_json(const pp_profile_t *profile,
                         const pp_profile_images_t *images)
{
    if (!profile) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "version", PP_PROFILE_VERSION);
    add_string(root, "name", profile->name);
    add_string(root, "tagline", profile->tagline);

    cJSON *platforms = cJSON_AddObjectToObject(root, "platforms");
    cJSON *steam = platforms ? cJSON_AddObjectToObject(platforms, "steam") : NULL;
    cJSON *switch_ = platforms ? cJSON_AddObjectToObject(platforms, "switch") : NULL;
    cJSON *psn = platforms ? cJSON_AddObjectToObject(platforms, "playstation") : NULL;
    if (!platforms || !steam || !switch_ || !psn) {
        cJSON_Delete(root);
        return NULL;
    }
    add_string(steam, "id", profile->steam_id);
    add_string(switch_, "id", profile->switch_id);
    add_string(psn, "id", profile->psn_id);

    cJSON *contact = cJSON_AddObjectToObject(root, "contact");
    cJSON *weibo = contact ? cJSON_AddObjectToObject(contact, "weibo") : NULL;
    cJSON *wechat = contact ? cJSON_AddObjectToObject(contact, "wechat") : NULL;
    if (!contact || !weibo || !wechat) {
        cJSON_Delete(root);
        return NULL;
    }
    add_string(weibo, "handle", profile->weibo_handle);
    add_string(weibo, "url", profile->weibo_url);
    add_string(wechat, "name", profile->wechat_name);
    add_string(wechat, "id", profile->wechat_id);

    if (images) {
        cJSON *flags = cJSON_AddObjectToObject(root, "images");
        if (!flags) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddBoolToObject(flags, "avatar", images->avatar);
        cJSON_AddBoolToObject(flags, "steam_qr", images->steam_qr);
        cJSON_AddBoolToObject(flags, "switch_qr", images->switch_qr);
        cJSON_AddBoolToObject(flags, "psn_qr", images->psn_qr);
        cJSON_AddBoolToObject(flags, "wechat_qr", images->wechat_qr);
        cJSON_AddBoolToObject(flags, "wechat_pay_qr", images->wechat_pay_qr);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

bool pp_profile_from_json(const char *json, pp_profile_t *profile, char *error,
                          size_t error_size)
{
    if (!json || !profile) {
        pp_set_error(error, error_size, "empty profile");
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        pp_set_error(error, error_size, "profile is not valid JSON");
        return false;
    }
    pp_profile_t parsed;
    pp_profile_defaults(&parsed);
    bool ok = false;

    if (!cJSON_IsObject(root)) {
        pp_set_error(error, error_size, "profile root must be an object");
        goto done;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valuedouble != PP_PROFILE_VERSION) {
        pp_set_error(error, error_size, "unsupported profile version");
        goto done;
    }
    if (!get_string(root, "name", parsed.name, sizeof(parsed.name),
                    PP_PROFILE_NAME_CP, false, "name", error, error_size) ||
        !get_string(root, "tagline", parsed.tagline, sizeof(parsed.tagline),
                    PP_PROFILE_TAGLINE_CP, false, "tagline", error,
                    error_size)) {
        goto done;
    }

    const cJSON *platforms =
        cJSON_GetObjectItemCaseSensitive(root, "platforms");
    if (platforms && !cJSON_IsObject(platforms)) {
        pp_set_error(error, error_size, "platforms must be an object");
        goto done;
    }
    if (platforms) {
        const struct {
            const char *key;
            char *dst;
            size_t size;
            const char *field;
        } platform_fields[] = {
            { "steam", parsed.steam_id, sizeof(parsed.steam_id), "steam.id" },
            { "switch", parsed.switch_id, sizeof(parsed.switch_id), "switch.id" },
            { "playstation", parsed.psn_id, sizeof(parsed.psn_id),
              "playstation.id" },
        };
        for (size_t i = 0;
             i < sizeof(platform_fields) / sizeof(platform_fields[0]); ++i) {
            const cJSON *item = cJSON_GetObjectItemCaseSensitive(
                platforms, platform_fields[i].key);
            if (!item || cJSON_IsNull(item)) continue;
            if (!cJSON_IsObject(item)) {
                pp_set_error(error, error_size, "platforms.%s must be an object",
                             platform_fields[i].key);
                goto done;
            }
            if (!get_string(item, "id", platform_fields[i].dst,
                            platform_fields[i].size, PP_PROFILE_PLATFORM_ID_CP,
                            false, platform_fields[i].field, error,
                            error_size)) {
                goto done;
            }
        }
    }

    const cJSON *contact = cJSON_GetObjectItemCaseSensitive(root, "contact");
    if (contact && !cJSON_IsObject(contact)) {
        pp_set_error(error, error_size, "contact must be an object");
        goto done;
    }
    if (contact) {
        const cJSON *weibo = cJSON_GetObjectItemCaseSensitive(contact, "weibo");
        const cJSON *wechat = cJSON_GetObjectItemCaseSensitive(contact, "wechat");
        if (weibo && !cJSON_IsNull(weibo)) {
            if (!cJSON_IsObject(weibo)) {
                pp_set_error(error, error_size, "contact.weibo must be an object");
                goto done;
            }
            if (!get_string(weibo, "handle", parsed.weibo_handle,
                            sizeof(parsed.weibo_handle),
                            PP_PROFILE_WEIBO_HANDLE_CP, false,
                            "contact.weibo.handle", error, error_size) ||
                !get_string(weibo, "url", parsed.weibo_url,
                            sizeof(parsed.weibo_url), 480, false,
                            "contact.weibo.url", error, error_size)) {
                goto done;
            }
        }
        if (wechat && !cJSON_IsNull(wechat)) {
            if (!cJSON_IsObject(wechat)) {
                pp_set_error(error, error_size,
                             "contact.wechat must be an object");
                goto done;
            }
            if (!get_string(wechat, "name", parsed.wechat_name,
                            sizeof(parsed.wechat_name), PP_PROFILE_WECHAT_NAME_CP,
                            false, "contact.wechat.name", error, error_size) ||
                !get_string(wechat, "id", parsed.wechat_id,
                            sizeof(parsed.wechat_id), PP_PROFILE_WECHAT_ID_CP,
                            false, "contact.wechat.id", error, error_size)) {
                goto done;
            }
        }
    }

    if (!pp_profile_validate(&parsed, error, error_size)) goto done;
    *profile = parsed;
    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}
