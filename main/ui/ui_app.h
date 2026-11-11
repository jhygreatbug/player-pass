// Device UI entry points used by the application core.
//
// All functions must be called from the input worker while holding
// bsp_lvgl_lock().  The UI never touches Wi-Fi or storage writes; it only reads
// the models provided by the core and loads image files for the current page.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_button.h"
#include "data/games.h"
#include "data/profile.h"
#include "image/image_store.h"
#include "storage/storage.h"

typedef enum {
    UI_ACTION_NONE = 0,
    UI_ACTION_ENTER_MANAGE,
    UI_ACTION_EXIT_MANAGE,
    UI_ACTION_RESTART,
} ui_action_t;

typedef enum {
    UI_MANAGE_STARTING = 0,
    UI_MANAGE_READY,
    UI_MANAGE_FAILED,
} ui_manage_state_t;

typedef struct {
    ui_manage_state_t state;
    char ssid[33];
    char password[64];
    char url[40];
    bool client_connected;
    bool lease_active;
    bool reserved;
    char client_name[49];
    int idle_remaining_s;
    int storage_percent;
    int storage_level;
    uint64_t used_bytes;
    uint64_t total_bytes;
    char error[128];
} ui_manage_info_t;

void ui_app_set_data(const pp_storage_t *storage, const pp_profile_t *profile,
                     const pp_profile_images_t *images,
                     const pp_games_t *games);
void ui_app_set_battery(int soc_percent);

void ui_app_show_boot(void);
void ui_app_show_error(const char *title, const char *body);
void ui_app_show_home(void);
void ui_app_show_startup_notice(const char *title, const char *body);

void ui_app_show_manage(const ui_manage_info_t *info);
void ui_app_update_manage(const ui_manage_info_t *info);

ui_action_t ui_app_handle_key(bsp_btn_t button, bsp_btn_ev_t event);
