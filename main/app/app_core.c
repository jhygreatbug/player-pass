#include "app/app_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/text_utf8.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "cJSON.h"
#include "data/games.h"
#include "data/profile.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "http_server/http_server.h"
#include "image/image_store.h"
#include "lvgl.h"
#include "key_guard.h"
#include "session/mgmt_session.h"
#include "storage/storage.h"
#include "storage/storage_budget.h"
#include "storage/storage_fs.h"
#include "ui/ui_app.h"
#include "wifi_manager/wifi_manager.h"

static const char *TAG = "app";

#define INPUT_STACK_BYTES 8192
#define INPUT_QUEUE_DEPTH 8
#define PROFILE_FILE_MAX (16u * 1024u)
#define GAMES_FILE_MAX (48u * 1024u)
#define BATTERY_REFRESH_MS (30 * 1000)
#define DIM_DELAY_MS (60 * 1000)

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} key_event_t;

static pp_storage_t s_storage;
static bool s_storage_ready;
static pp_profile_t s_profile;
static pp_profile_images_t s_images;
static pp_games_t s_games;
static char s_data_warning[256];

static QueueHandle_t s_key_queue;
static TaskHandle_t s_input_task;
static volatile bool s_input_ready;
static int s_battery_soc = -1;
static int64_t s_last_battery_ms;
static int64_t s_last_input_ms;
static bool s_dimmed;

static SemaphoreHandle_t s_session_lock;
static pp_mgmt_session_t s_session;
static bool s_managing;
static bool s_manage_failed;
static char s_manage_error[128];
static pp_wifi_status_t s_wifi_status;
static pp_http_context_t s_http_context;
static int64_t s_last_manage_ui_ms;
static key_guard_t s_key_guard;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "%s: free=%u largest=%u", stage,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void log_lvgl_pool(const char *stage)
{
    lv_mem_monitor_t monitor;
    lv_mem_monitor(&monitor);
    ESP_LOGI(TAG, "%s: LVGL pool total=%u used=%u max_used=%u frag=%u%%",
             stage, (unsigned)monitor.total_size,
             (unsigned)(monitor.total_size - monitor.free_size),
             (unsigned)monitor.max_used, (unsigned)monitor.frag_pct);
}

static void add_warning(const char *format, ...)
{
    const size_t used = strlen(s_data_warning);
    if (used + 4 >= sizeof(s_data_warning)) return;
    if (used > 0) {
        snprintf(s_data_warning + used, sizeof(s_data_warning) - used, "；");
    }
    const size_t offset = strlen(s_data_warning);
    va_list args;
    va_start(args, format);
    (void)vsnprintf(s_data_warning + offset, sizeof(s_data_warning) - offset,
                    format, args);
    va_end(args);
}

static char *read_file_alloc(const char *rel, size_t max, bool *missing)
{
    *missing = false;
    size_t size = 0;
    const pp_storage_err_t err = pp_storage_size(&s_storage, rel, &size);
    if (err == PP_STORAGE_ERR_NOT_FOUND) {
        *missing = true;
        return NULL;
    }
    if (err != PP_STORAGE_OK) {
        add_warning("无法读取 %s", rel);
        return NULL;
    }
    if (size > max) {
        add_warning("%s 文件过大", rel);
        return NULL;
    }
    char *buffer = malloc(size + 1);
    if (!buffer) {
        add_warning("内存不足，无法读取 %s", rel);
        return NULL;
    }
    size_t length = 0;
    if (pp_storage_read(&s_storage, rel, buffer, size + 1, &length) !=
        PP_STORAGE_OK) {
        add_warning("读取 %s 失败", rel);
        free(buffer);
        return NULL;
    }
    return buffer;
}

static void write_default(const char *rel, char *json)
{
    if (!json) return;
    const pp_storage_err_t err = pp_storage_write_atomic(
        &s_storage, rel, json, strlen(json));
    if (err != PP_STORAGE_OK) {
        add_warning("无法初始化 %s", rel);
    }
    cJSON_free(json);
}

static void load_profile(void)
{
    pp_profile_defaults(&s_profile);
    bool missing = false;
    char *json = read_file_alloc("data/profile.json", PROFILE_FILE_MAX, &missing);
    if (missing) {
        char *defaults = pp_profile_to_json(&s_profile, NULL);
        write_default("data/profile.json", defaults);
        return;
    }
    if (!json) return;
    char error[128];
    if (!pp_profile_from_json(json, &s_profile, error, sizeof(error))) {
        pp_profile_defaults(&s_profile);
        add_warning("个人资料文件异常：%s", error);
    }
    free(json);
}

static void load_games(void)
{
    pp_games_free(&s_games);
    pp_games_init(&s_games);
    bool missing = false;
    char *json = read_file_alloc("data/games.json", GAMES_FILE_MAX, &missing);
    if (missing) {
        char *defaults = pp_games_to_json(&s_games, false);
        write_default("data/games.json", defaults);
        return;
    }
    if (!json) return;
    char error[128];
    pp_games_t parsed;
    pp_games_init(&parsed);
    if (!pp_games_from_json(json, &parsed, error, sizeof(error))) {
        pp_games_free(&parsed);
        add_warning("游戏列表文件异常：%s", error);
    } else {
        pp_games_free(&s_games);
        s_games = parsed;
    }
    free(json);
}

static void load_data(void)
{
    s_data_warning[0] = '\0';
    pp_profile_images_clear(&s_images);
    load_profile();
    load_games();
    if (pp_image_profile_flags(&s_storage, &s_images) != PP_STORAGE_OK) {
        add_warning("无法检查个人图片");
    }
    if (pp_image_game_flags(&s_storage, &s_games) != PP_STORAGE_OK) {
        add_warning("无法检查游戏封面");
    }
}

/* -------------------------------- input --------------------------------- */

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    // A key held at power-on (or after a future deep-sleep wake) is already down
    // when the button component starts and can be graded as a long press almost
    // immediately.  Consume events until that key is released, but run the guard
    // before the readiness check so a release arriving before dispatch is ready
    // still disarms it.
    const bool was_armed = s_key_guard.armed;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE,
                          esp_timer_get_time())) {
        if (was_armed && !s_key_guard.armed) {
            ESP_LOGI(TAG, "启动按键已松手，按键恢复响应");
        }
        return;
    }
    if (!s_input_ready || !s_key_queue) return;
    const key_event_t key = { .button = button, .event = event };
    (void)xQueueSend(s_key_queue, &key, 0);
}

static ui_manage_info_t build_manage_info(void)
{
    ui_manage_info_t info;
    memset(&info, 0, sizeof(info));
    info.state = s_manage_failed
                     ? UI_MANAGE_FAILED
                     : (pp_wifi_manager_running() && pp_http_server_running()
                            ? UI_MANAGE_READY
                            : UI_MANAGE_STARTING);
    snprintf(info.ssid, sizeof(info.ssid), "%s", s_wifi_status.ssid);
    snprintf(info.password, sizeof(info.password), "%s", s_wifi_status.password);
    if (s_wifi_status.ip[0]) {
        snprintf(info.url, sizeof(info.url), "http://%s", s_wifi_status.ip);
    }
    info.client_connected = pp_wifi_manager_client_count() > 0;
    if (s_manage_error[0]) {
        snprintf(info.error, sizeof(info.error), "%.127s", s_manage_error);
    }

    pp_mgmt_status_t status;
    xSemaphoreTake(s_session_lock, portMAX_DELAY);
    pp_mgmt_session_status(&s_session, now_ms(), "", "", &status);
    xSemaphoreGive(s_session_lock);
    info.lease_active = status.occupied && status.grace_remaining_s == 0;
    info.reserved = status.occupied && status.grace_remaining_s > 0;
    snprintf(info.client_name, sizeof(info.client_name), "%s", status.client_name);
    info.idle_remaining_s = status.idle_remaining_s;

    uint64_t used = 0;
    uint64_t total = 0;
    if (pp_storage_usage(&s_storage, &used, &total) == PP_STORAGE_OK) {
        info.used_bytes = used;
        info.total_bytes = total;
        info.storage_percent = pp_storage_percent(used, total);
        info.storage_level = (int)pp_storage_level(used, total);
    }
    return info;
}

static void management_update_ui(void)
{
    const ui_manage_info_t info = build_manage_info();
    if (bsp_lvgl_lock(500)) {
        ui_app_update_manage(&info);
        bsp_lvgl_unlock();
    }
}

static void management_exit(void)
{
    if (!s_managing) return;
    ESP_LOGI(TAG, "退出管理模式");
    s_managing = false;
    s_manage_failed = false;
    s_manage_error[0] = '\0';
    pp_http_server_stop();
    pp_wifi_manager_stop();
    xSemaphoreTake(s_session_lock, portMAX_DELAY);
    pp_mgmt_session_stop(&s_session);
    xSemaphoreGive(s_session_lock);

    load_data();
    if (bsp_lvgl_lock(1000)) {
        ui_app_set_data(&s_storage, &s_profile, &s_images, &s_games);
        ui_app_set_battery(s_battery_soc);
        ui_app_show_home();
        if (s_data_warning[0]) {
            ui_app_show_startup_notice("数据文件提示", s_data_warning);
        }
        bsp_lvgl_unlock();
    }
    bsp_display_backlight(100);
    s_dimmed = false;
    s_last_input_ms = now_ms();
    log_memory("management stopped");
}

static void management_enter(void)
{
    if (s_managing) return;
    if (!s_storage_ready) {
        ESP_LOGE(TAG, "存储不可用，无法进入管理模式");
        return;
    }
    ESP_LOGI(TAG, "进入管理模式");
    s_managing = true;
    s_manage_failed = false;
    s_manage_error[0] = '\0';
    memset(&s_wifi_status, 0, sizeof(s_wifi_status));

    xSemaphoreTake(s_session_lock, portMAX_DELAY);
    pp_mgmt_session_start(&s_session, now_ms(), 0,
                          PP_MGMT_LEASE_TIMEOUT_MS, PP_MGMT_GRACE_MS);
    xSemaphoreGive(s_session_lock);

    ui_manage_info_t info = build_manage_info();
    if (bsp_lvgl_lock(1000)) {
        ui_app_show_manage(&info);
        bsp_lvgl_unlock();
    }
    s_last_manage_ui_ms = now_ms();
    bsp_display_backlight(100);
    s_dimmed = false;

    esp_err_t err = pp_wifi_manager_start(&s_wifi_status);
    if (err != ESP_OK) {
        s_manage_failed = true;
        snprintf(s_manage_error, sizeof(s_manage_error), "热点：%s",
                 esp_err_to_name(err));
        management_update_ui();
        return;
    }

    log_memory("AP/HTTP starting");
    s_http_context = (pp_http_context_t){
        .storage = &s_storage,
        .profile = &s_profile,
        .images = &s_images,
        .games = &s_games,
        .session = &s_session,
        .session_lock = s_session_lock,
        .ap_ssid = s_wifi_status.ssid,
        .ap_ip = s_wifi_status.ip,
        .client_count = pp_wifi_manager_client_count,
    };
    err = pp_http_server_start(&s_http_context);
    if (err != ESP_OK) {
        s_manage_failed = true;
        snprintf(s_manage_error, sizeof(s_manage_error), "管理页面：%s",
                 esp_err_to_name(err));
        pp_wifi_manager_stop();
        management_update_ui();
        return;
    }
    // Rebuild once the URL is known so the management screen can render its
    // access QR; later ticks only update labels.
    const ui_manage_info_t ready = build_manage_info();
    if (bsp_lvgl_lock(1000)) {
        ui_app_show_manage(&ready);
        bsp_lvgl_unlock();
    }
    s_last_manage_ui_ms = now_ms();
    log_memory("management ready");
}

static void refresh_battery(void)
{
    s_battery_soc = bsp_battery_soc();
    if (bsp_lvgl_lock(300)) {
        ui_app_set_battery(s_battery_soc);
        bsp_lvgl_unlock();
    }
    s_last_battery_ms = now_ms();
}

static void handle_tick(void)
{
    const int64_t now = now_ms();
    if (s_managing) {
        bool should_exit = false;
        xSemaphoreTake(s_session_lock, portMAX_DELAY);
        should_exit = pp_mgmt_session_tick(&s_session, now);
        xSemaphoreGive(s_session_lock);
        if (should_exit) {
            management_exit();
            return;
        }
        if (now - s_last_manage_ui_ms >= 1000) {
            management_update_ui();
            s_last_manage_ui_ms = now;
        }
    }
    if (now - s_last_battery_ms >= BATTERY_REFRESH_MS) {
        refresh_battery();
    }
    if (!s_managing && !s_dimmed && now - s_last_input_ms >= DIM_DELAY_MS) {
        bsp_display_backlight(30);
        s_dimmed = true;
    }
}

static void handle_key(const key_event_t *key)
{
    const int64_t now = now_ms();
    s_last_input_ms = now;
    if (s_dimmed) {
        bsp_display_backlight(100);
        s_dimmed = false;
    }
    if (key->event != BSP_BTN_CLICK && key->event != BSP_BTN_DOUBLE &&
        key->event != BSP_BTN_LONG) {
        return;
    }
    if (!bsp_lvgl_lock(300)) return;
    const ui_action_t action = ui_app_handle_key(key->button, key->event);
    bsp_lvgl_unlock();

    switch (action) {
    case UI_ACTION_ENTER_MANAGE:
        management_enter();
        break;
    case UI_ACTION_EXIT_MANAGE:
        management_exit();
        break;
    case UI_ACTION_RESTART:
        esp_restart();
        break;
    default:
        break;
    }
}

static void input_task(void *argument)
{
    (void)argument;
    key_event_t key;
    for (;;) {
        if (xQueueReceive(s_key_queue, &key, pdMS_TO_TICKS(200)) == pdTRUE) {
            handle_key(&key);
        }
        handle_tick();
    }
}

/* -------------------------------- start --------------------------------- */

static void show_boot_screen(void)
{
    if (bsp_lvgl_lock(1000)) {
        ui_app_show_boot();
        bsp_lvgl_unlock();
    }
}

static bool start_services(void)
{
    s_session_lock = xSemaphoreCreateMutex();
    if (!s_session_lock) return false;
    s_key_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(key_event_t));
    if (!s_key_queue) return false;
    if (xTaskCreate(input_task, "app_input", INPUT_STACK_BYTES, NULL, 6,
                    &s_input_task) != pdPASS) {
        return false;
    }
    return true;
}

static bool init_battery_and_buttons(void)
{
    esp_err_t battery = bsp_battery_init();
    if (battery != ESP_OK) {
        ESP_LOGW(TAG, "电量计不可用: %s", esp_err_to_name(battery));
    }
    s_battery_soc = bsp_battery_soc();
    s_last_battery_ms = now_ms();

    esp_err_t buttons = bsp_button_init(on_key, NULL);
    if (buttons == ESP_OK) {
        // If no key is held, no release event will ever arrive; sample the
        // physical level once to disarm the guard immediately.
        const int mv = bsp_button_read_mv();
        key_guard_sync_released(&s_key_guard, mv >= BSP_BTN_MV_RELEASED_MIN);
    } else {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(buttons));
        if (bsp_lvgl_lock(1000)) {
            ui_app_show_error("按键异常",
                              "按键初始化失败。请检查硬件后重启设备。");
            bsp_lvgl_unlock();
        }
        return false;
    }
    return true;
}

static void show_home_or_warning(void)
{
    if (!bsp_lvgl_lock(1000)) return;
    ui_app_set_data(&s_storage, &s_profile, &s_images, &s_games);
    ui_app_set_battery(s_battery_soc);
    ui_app_show_home();
    if (s_data_warning[0]) {
        ui_app_show_startup_notice("数据文件提示", s_data_warning);
    }
    log_lvgl_pool("home");
    bsp_lvgl_unlock();
}

void app_core_start(void)
{
    s_last_input_ms = now_ms();
    // Arm before the button driver starts observing; the post-init voltage
    // sample disarms it when nothing is held.  The deadline only protects
    // against a stuck key leaving the keypad dead.
    key_guard_arm(&s_key_guard, esp_timer_get_time(), 10LL * 1000 * 1000);
    show_boot_screen();

    if (!start_services()) {
        ESP_LOGE(TAG, "输入任务创建失败");
        if (bsp_lvgl_lock(1000)) {
            ui_app_show_error("启动失败", "输入任务创建失败，请重启设备。");
            bsp_lvgl_unlock();
        }
        return;
    }

    const esp_err_t mount = pp_storage_fs_mount(&s_storage);
    if (mount != ESP_OK) {
        s_storage_ready = false;
        const bool buttons_ok = init_battery_and_buttons();
        s_input_ready = buttons_ok;
        char body[160];
        snprintf(body, sizeof(body),
                 "数据分区挂载失败（%s）。用户资料不会自动格式化，请检查串口日志。",
                 esp_err_to_name(mount));
        if (bsp_lvgl_lock(1000)) {
            ui_app_show_error("存储异常", body);
            bsp_lvgl_unlock();
        }
        return;
    }

    s_storage_ready = true;
    load_data();
    if (!init_battery_and_buttons()) {
        return;
    }
    show_home_or_warning();
    s_input_ready = true;
    log_memory("boot complete");
    ESP_LOGI(TAG, "应用就绪，电量=%d", s_battery_soc);
}
