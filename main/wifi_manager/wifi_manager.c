#include "wifi_manager/wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_mgr";

#define AP_STARTED_BIT BIT0
// Fixed product password, displayed on the management screen.  Change this
// constant and rebuild to rotate it; there is no per-device secret.
#define AP_PASSWORD "playerpass"

static bool s_wifi_initialized;
static bool s_wifi_started;
static esp_netif_t *s_ap_netif;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static EventGroupHandle_t s_events;
static volatile int s_client_count;

static bool ensure_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) return true;
    // Never erase NVS to hide a partition problem: application data would be
    // lost silently.  Report the real error and let the UI explain it.
    ESP_LOGE(TAG, "NVS 初始化失败: %s", esp_err_to_name(err));
    return false;
}

esp_err_t pp_wifi_manager_password(char *out, size_t out_size)
{
    if (!out || out_size < sizeof(AP_PASSWORD)) return ESP_ERR_INVALID_ARG;
    snprintf(out, out_size, "%s", AP_PASSWORD);
    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch (id) {
    case WIFI_EVENT_AP_START:
        if (s_events) xEventGroupSetBits(s_events, AP_STARTED_BIT);
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        if (s_client_count < 8) ++s_client_count;
        ESP_LOGI(TAG, "管理客户端已连接 (count=%d)", s_client_count);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        if (s_client_count > 0) --s_client_count;
        ESP_LOGI(TAG, "管理客户端已断开 (count=%d)", s_client_count);
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id,
                             void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_ap_staipassigned_t *event =
        (const ip_event_ap_staipassigned_t *)data;
    if (event) {
        ESP_LOGI(TAG, "客户端已取得 IP: " IPSTR, IP2STR(&event->ip));
    }
}

static void stop_locked(void)
{
    if (s_wifi_handler) {
        (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    s_wifi_handler);
        s_wifi_handler = NULL;
    }
    if (s_ip_handler) {
        (void)esp_event_handler_instance_unregister(IP_EVENT,
                                                    IP_EVENT_AP_STAIPASSIGNED,
                                                    s_ip_handler);
        s_ip_handler = NULL;
    }
    if (s_wifi_started) {
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    if (s_wifi_initialized) {
        (void)esp_wifi_deinit();
        s_wifi_initialized = false;
    }
    if (s_ap_netif) {
        esp_netif_destroy_default_wifi(s_ap_netif);
        s_ap_netif = NULL;
    }
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = NULL;
    }
    s_client_count = 0;
}

void pp_wifi_manager_stop(void)
{
    stop_locked();
    ESP_LOGI(TAG, "热点已停止");
}

bool pp_wifi_manager_running(void)
{
    return s_wifi_started;
}

int pp_wifi_manager_client_count(void)
{
    return s_client_count;
}

static esp_err_t make_ssid(char *ssid, size_t size)
{
    uint8_t mac[6] = { 0 };
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    const int written = snprintf(ssid, size, "PlayerPass-%02X%02X", mac[4], mac[5]);
    return written > 0 && (size_t)written < size ? ESP_OK : ESP_FAIL;
}

esp_err_t pp_wifi_manager_start(pp_wifi_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (s_wifi_started) return ESP_ERR_INVALID_STATE;

    if (!ensure_nvs()) return ESP_FAIL;

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "netif 初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "事件循环创建失败: %s", esp_err_to_name(err));
        return err;
    }

    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) {
        ESP_LOGE(TAG, "AP netif 创建失败");
        stop_locked();
        return ESP_FAIL;
    }

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi 初始化失败: %s", esp_err_to_name(err));
        stop_locked();
        return err;
    }
    s_wifi_initialized = true;

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, NULL,
                                              &s_wifi_handler);
    if (err != ESP_OK) goto fail;
    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_AP_STAIPASSIGNED,
                                              ip_event_handler, NULL,
                                              &s_ip_handler);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) goto fail;

    char ssid[33];
    err = make_ssid(ssid, sizeof(ssid));
    if (err != ESP_OK) goto fail;
    char password[64];
    err = pp_wifi_manager_password(password, sizeof(password));
    if (err != ESP_OK) goto fail;

    wifi_config_t config = { 0 };
    const size_t ssid_len = strlen(ssid);
    const size_t password_len = strlen(password);
    if (ssid_len > sizeof(config.ap.ssid) ||
        password_len + 1 > sizeof(config.ap.password)) {
        err = ESP_ERR_INVALID_ARG;
        goto fail;
    }
    memcpy(config.ap.ssid, ssid, ssid_len);
    config.ap.ssid_len = (uint8_t)ssid_len;
    memcpy(config.ap.password, password, password_len + 1);
    config.ap.channel = 1;
    config.ap.max_connection = 3;
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.pmf_cfg.required = false;
    err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err != ESP_OK) goto fail;
    err = esp_wifi_start();
    if (err != ESP_OK) goto fail;
    s_wifi_started = true;

    const EventBits_t bits = xEventGroupWaitBits(s_events, AP_STARTED_BIT,
                                                 pdFALSE, pdTRUE,
                                                 pdMS_TO_TICKS(5000));
    if (!(bits & AP_STARTED_BIT)) {
        ESP_LOGE(TAG, "等待 AP_START 超时");
        err = ESP_ERR_TIMEOUT;
        goto fail;
    }

    esp_netif_ip_info_t ip_info = { 0 };
    err = esp_netif_get_ip_info(s_ap_netif, &ip_info);
    if (err != ESP_OK) goto fail;

    status->started = true;
    snprintf(status->ssid, sizeof(status->ssid), "%s", ssid);
    snprintf(status->password, sizeof(status->password), "%s", password);
    snprintf(status->ip, sizeof(status->ip), IPSTR, IP2STR(&ip_info.ip));
    status->client_count = s_client_count;
    ESP_LOGI(TAG, "热点已启动: SSID=%s IP=%s", status->ssid, status->ip);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "热点启动失败: %s", esp_err_to_name(err));
    stop_locked();
    return err;
}
