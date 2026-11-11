#include "storage/storage_fs.h"

#include <string.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "storage_fs";

#define DATA_PARTITION_LABEL "storage"
#define MOUNT_POINT "/lfs"

static bool partition_blank(const esp_partition_t *partition)
{
    uint8_t head[256];
    if (esp_partition_read(partition, 0, head, sizeof(head)) != ESP_OK) {
        return false;
    }
    for (size_t i = 0; i < sizeof(head); ++i) {
        if (head[i] != 0xFF) return false;
    }
    return true;
}

esp_err_t pp_storage_fs_mount(pp_storage_t *storage)
{
    if (!storage) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
        DATA_PARTITION_LABEL);
    if (!partition) {
        ESP_LOGE(TAG, "找不到数据分区 %s", DATA_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    const bool blank = partition_blank(partition);

    esp_vfs_littlefs_conf_t config = {
        .base_path = MOUNT_POINT,
        .partition_label = DATA_PARTITION_LABEL,
        .format_if_mount_failed = false,
        .read_only = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&config);
    if (err != ESP_OK && blank) {
        ESP_LOGW(TAG, "数据分区为空白，首次格式化");
        err = esp_littlefs_format(DATA_PARTITION_LABEL);
        if (err == ESP_OK) err = esp_vfs_littlefs_register(&config);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS 挂载失败: %s (blank=%d)",
                 esp_err_to_name(err), blank);
        return blank ? err : ESP_ERR_INVALID_STATE;
    }

    const pp_storage_err_t init = pp_storage_init(storage, MOUNT_POINT);
    if (init != PP_STORAGE_OK) {
        ESP_LOGE(TAG, "数据目录初始化失败: %s", pp_storage_error_name(init));
        (void)esp_vfs_littlefs_unregister(DATA_PARTITION_LABEL);
        return ESP_FAIL;
    }
    size_t total = 0;
    size_t used = 0;
    if (esp_littlefs_info(DATA_PARTITION_LABEL, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS 就绪: %u/%u 字节已用",
                 (unsigned)used, (unsigned)total);
    }
    return ESP_OK;
}
