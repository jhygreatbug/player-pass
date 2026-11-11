// LittleFS mount for the application data partition.
//
// A blank partition (all 0xFF) is formatted once on first boot.  A non-blank
// partition that fails to mount is treated as corrupt and is never formatted
// automatically, so a filesystem error cannot silently destroy user data.
#pragma once

#include "esp_err.h"
#include "storage/storage.h"

esp_err_t pp_storage_fs_mount(pp_storage_t *storage);
