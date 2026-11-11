#include "ui/ui_app.h"

#include <stdio.h>
#include <string.h>

#include "app/text_utf8.h"
#include "esp_log.h"
#include "font/pp_font.h"
#include "image/image_view.h"
#include "image/qr_render.h"
#include "ui/icons.h"
#include "ui/ui_theme.h"

static const char *TAG = "ui_app";

typedef enum {
    PAGE_NONE = 0,
    PAGE_BOOT,
    PAGE_HOME,
    PAGE_GAMES,
    PAGE_DETAIL,
    PAGE_CONTACT,
    PAGE_NFC,
    PAGE_OVERLAY,
    PAGE_MANAGE,
    PAGE_ERROR,
} page_t;

typedef enum {
    ENTRY_HEADER = 0,
    ENTRY_GAME,
    ENTRY_EMPTY,
} entry_kind_t;

#define HOME_PLATFORMS 3
#define HOME_NAV 3
#define HOME_ITEMS (HOME_PLATFORMS + HOME_NAV)

#define GAME_WINDOW_ENTRIES 5
#define GAME_COVER_SLOTS GAME_WINDOW_ENTRIES

typedef struct {
    entry_kind_t kind;
    pp_game_group_t group;
    size_t game_index;
} game_entry_t;

typedef struct {
    bool used;
    bool loaded;
    size_t game_index;
    pp_loaded_image_t image;
} cover_slot_t;

static const pp_storage_t *s_storage;
static const pp_profile_t *s_profile;
static const pp_profile_images_t *s_images;
static const pp_games_t *s_games;
static const pp_nfc_t *s_nfc;
static int s_battery_soc = -1;

static page_t s_page = PAGE_NONE;
static lv_obj_t *s_screen;
static pp_topbar_t *s_active_bar;

static pp_topbar_t s_home_bar;
static pp_topbar_t s_games_bar;
static pp_topbar_t s_detail_bar;
static pp_topbar_t s_contact_bar;
static pp_topbar_t s_nfc_bar;
static pp_topbar_t s_overlay_bar;
static pp_topbar_t s_manage_bar;
static pp_topbar_t s_error_bar;

// Defined with the overlay section; declared here for destroy_all_pages().
static lv_obj_t *s_overlay_return_screen;
static pp_topbar_t *s_overlay_return_bar;

static void set_current(lv_obj_t *screen, page_t page, pp_topbar_t *bar)
{
    s_screen = screen;
    s_page = page;
    s_active_bar = bar;
    if (bar) pp_topbar_update(bar, s_battery_soc);
    if (screen) lv_screen_load(screen);
}

static void free_home_avatar(void);
static void free_game_covers(void);
static void free_detail_cover(void);
static void free_overlay_images(void);
static void free_manage_qr(void);

static void delete_current_screen(void)
{
    if (!s_screen) return;
    lv_obj_delete(s_screen);
    s_screen = NULL;
    s_active_bar = NULL;
}

static void destroy_all_pages(void)
{
    delete_current_screen();
    if (s_overlay_return_screen && s_overlay_return_screen != s_screen) {
        lv_obj_delete(s_overlay_return_screen);
    }
    s_overlay_return_screen = NULL;
    s_overlay_return_bar = NULL;
    free_home_avatar();
    free_game_covers();
    free_detail_cover();
    free_overlay_images();
    free_manage_qr();
    s_page = PAGE_NONE;
}

static lv_obj_t *dyn_label(lv_obj_t *parent, const lv_font_t *font,
                           uint32_t color, int x, int y, int width, int height,
                           const char *text)
{
    lv_obj_t *label = pp_label_create(parent, "", font, color, x, y, width);
    if (!label) return NULL;
    lv_obj_set_height(label, height);
    pp_label_set_text_safe(label, text);
    return label;
}

static void set_text(lv_obj_t *label, const char *text)
{
    pp_label_set_text_safe(label, text ? text : "");
}

/* ------------------------------------------------------------------------- */
/* Home                                                                      */
/* ------------------------------------------------------------------------- */

static int s_home_sel;
static lv_obj_t *s_home_platform_panel[HOME_PLATFORMS];
static lv_obj_t *s_home_platform_name[HOME_PLATFORMS];
static lv_obj_t *s_home_platform_value[HOME_PLATFORMS];
static lv_obj_t *s_home_platform_icon[HOME_PLATFORMS];
static lv_obj_t *s_home_platform_qr[HOME_PLATFORMS];
static lv_obj_t *s_home_nav_panel[HOME_NAV];
static lv_obj_t *s_home_nav_label[HOME_NAV];
static lv_obj_t *s_home_nav_icon[HOME_NAV];
static lv_obj_t *s_home_nav_chevron[HOME_NAV];
static pp_loaded_image_t s_home_avatar;
static bool s_home_avatar_loaded;

static const pp_icon_id_t PLATFORM_ICONS[HOME_PLATFORMS] = {
    PP_ICON_GAMEPAD, PP_ICON_HANDHELD, PP_ICON_SHAPES,
};
static const char *const PLATFORM_NAMES[HOME_PLATFORMS] = {
    "Steam", "Switch", "PSN",
};
static const pp_icon_id_t NAV_ICONS[HOME_NAV] = {
    PP_ICON_CARTRIDGE, PP_ICON_CHAT, PP_ICON_TAG,
};
static const char *const NAV_NAMES[HOME_NAV] = {
    "游戏收藏", "联系方式", "NFC 名片",
};

static const char *platform_id(int index)
{
    if (!s_profile) return "";
    switch (index) {
    case 0: return s_profile->steam_id;
    case 1: return s_profile->switch_id;
    case 2: return s_profile->psn_id;
    default: return "";
    }
}

static bool platform_has_qr(int index)
{
    if (!s_images) return false;
    switch (index) {
    case 0: return s_images->steam_qr;
    case 1: return s_images->switch_qr;
    case 2: return s_images->psn_qr;
    default: return false;
    }
}

static void free_home_avatar(void)
{
    if (s_home_avatar_loaded) {
        pp_image_free(&s_home_avatar);
        s_home_avatar_loaded = false;
    }
}

static void home_refresh(void)
{
    for (int i = 0; i < HOME_PLATFORMS; ++i) {
        const bool selected = s_home_sel == i;
        pp_row_style(s_home_platform_panel[i], selected);
        pp_row_text_color(s_home_platform_name[i], selected);
        const bool has_value = platform_id(i)[0] != '\0';
        lv_obj_set_style_text_color(
            s_home_platform_value[i],
            lv_color_hex(selected ? PP_COLOR_MINT
                                  : (has_value ? PP_COLOR_TEXT : PP_COLOR_DIM)),
            0);
        pp_icon_recolor(s_home_platform_icon[i],
                        selected ? PP_COLOR_MINT : PP_COLOR_MUTED);
        if (s_home_platform_qr[i]) {
            if (platform_has_qr(i)) {
                lv_obj_clear_flag(s_home_platform_qr[i], LV_OBJ_FLAG_HIDDEN);
                pp_icon_recolor(s_home_platform_qr[i],
                                selected ? PP_COLOR_MINT : PP_COLOR_DIM);
            } else {
                lv_obj_add_flag(s_home_platform_qr[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    for (int i = 0; i < HOME_NAV; ++i) {
        const bool selected = s_home_sel == HOME_PLATFORMS + i;
        pp_row_style(s_home_nav_panel[i], selected);
        pp_row_text_color(s_home_nav_label[i], selected);
        pp_icon_recolor(s_home_nav_icon[i],
                        selected ? PP_COLOR_MINT : PP_COLOR_MUTED);
        pp_icon_recolor(s_home_nav_chevron[i],
                        selected ? PP_COLOR_MINT : PP_COLOR_DIM);
    }
}

static void build_home(void)
{
    destroy_all_pages();
    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_home_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_home_bar;

    lv_obj_t *frame = pp_panel_create(s_screen, 12, 30, 92, 92);
    char rel[PP_STORAGE_PATH_MAX];
    if (s_images && s_images->avatar && s_storage &&
        pp_image_avatar_path(rel, sizeof(rel)) &&
        pp_image_load(s_storage, rel, &s_home_avatar) == PP_STORAGE_OK) {
        s_home_avatar_loaded = true;
        pp_image_widget_create(frame, &s_home_avatar, 2, 2, 88, 88);
    } else {
        lv_obj_t *placeholder = pp_icon_create(frame, PP_ICON_USER,
                                               PP_COLOR_DIM, 30, 30);
        if (placeholder) lv_obj_set_size(placeholder, 32, 32);
    }

    const char *name = s_profile ? s_profile->name : "";
    lv_obj_t *name_label = dyn_label(s_screen, &pp_font_24, PP_COLOR_TEXT,
                                     112, 32, 116, 56,
                                     name[0] ? name : "未设置姓名");
    if (name_label) {
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_MODE_DOTS);
        if (!name[0]) {
            lv_obj_set_style_text_color(name_label, lv_color_hex(PP_COLOR_DIM),
                                        0);
        }
    }
    const char *tagline = s_profile ? s_profile->tagline : "";
    lv_obj_t *tagline_label = dyn_label(
        s_screen, &pp_font_12, tagline[0] ? PP_COLOR_MUTED : PP_COLOR_DIM, 112,
        90, 116, 32, tagline[0] ? tagline : "在管理后台补充一句话介绍");
    if (tagline_label) lv_label_set_long_mode(tagline_label, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *section = pp_label_create(s_screen, "平台账号", &pp_font_12,
                                        PP_COLOR_DIM, 12, 128, 120);
    (void)section;

    for (int i = 0; i < HOME_PLATFORMS; ++i) {
        lv_obj_t *panel = pp_panel_create(s_screen, 12, 146 + i * 22, 216, 20);
        s_home_platform_panel[i] = panel;
        s_home_platform_icon[i] = pp_icon_create(panel, PLATFORM_ICONS[i],
                                                 PP_COLOR_MUTED, 5, 3);
        if (s_home_platform_icon[i]) {
            lv_obj_set_size(s_home_platform_icon[i], 12, 12);
        }
        s_home_platform_name[i] = pp_label_create(
            panel, PLATFORM_NAMES[i], &pp_font_12, PP_COLOR_TEXT, 24, 2, 54);
        if (s_home_platform_name[i]) {
            lv_label_set_long_mode(s_home_platform_name[i], LV_LABEL_LONG_MODE_DOTS);
        }
        s_home_platform_value[i] = dyn_label(
            panel, &pp_font_12, PP_COLOR_TEXT, 80, 2, 110, 16,
            platform_id(i)[0] ? platform_id(i) : "未设置");
        if (s_home_platform_value[i]) {
            lv_obj_set_style_text_align(s_home_platform_value[i],
                                        LV_TEXT_ALIGN_RIGHT, 0);
            lv_label_set_long_mode(s_home_platform_value[i],
                                   LV_LABEL_LONG_MODE_DOTS);
        }
        s_home_platform_qr[i] = pp_icon_create(panel, PP_ICON_QR,
                                               PP_COLOR_DIM, 196, 4);
        if (s_home_platform_qr[i]) {
            lv_obj_set_size(s_home_platform_qr[i], 12, 12);
        }
    }

    pp_divider(s_screen, 214);

    for (int i = 0; i < HOME_NAV; ++i) {
        lv_obj_t *panel = pp_panel_create(s_screen, 12, 218 + i * 24, 216, 22);
        s_home_nav_panel[i] = panel;
        s_home_nav_icon[i] = pp_icon_create(panel, NAV_ICONS[i],
                                            PP_COLOR_MUTED, 5, 4);
        s_home_nav_label[i] = pp_label_create(panel, NAV_NAMES[i], &pp_font_12,
                                              PP_COLOR_TEXT, 26, 3, 156);
        if (s_home_nav_label[i]) {
            lv_label_set_long_mode(s_home_nav_label[i], LV_LABEL_LONG_MODE_DOTS);
        }
        s_home_nav_chevron[i] = pp_icon_create(panel, PP_ICON_CHEVRON,
                                               PP_COLOR_DIM, 198, 5);
        if (s_home_nav_chevron[i]) {
            lv_obj_set_size(s_home_nav_chevron[i], 12, 12);
        }
    }

    pp_footer(s_screen, "OK 进入 · 双击OK 管理模式");
    home_refresh();
    set_current(s_screen, PAGE_HOME, &s_home_bar);
}

/* ------------------------------------------------------------------------- */
/* Overlays: QR, text detail, notice                                         */
/* ------------------------------------------------------------------------- */

static lv_obj_t *s_overlay_return_screen;
static page_t s_overlay_return_page;
static pp_topbar_t *s_overlay_return_bar;
static pp_loaded_image_t s_overlay_uploaded;
static pp_qr_image_t s_overlay_generated;
static bool s_overlay_uploaded_loaded;
static bool s_overlay_generated_loaded;

static void free_overlay_images(void)
{
    if (s_overlay_uploaded_loaded) {
        pp_image_free(&s_overlay_uploaded);
        s_overlay_uploaded_loaded = false;
    }
    if (s_overlay_generated_loaded) {
        pp_qr_free(&s_overlay_generated);
        s_overlay_generated_loaded = false;
    }
}

static void remember_return_page(void)
{
    if (s_page == PAGE_OVERLAY || s_page == PAGE_MANAGE || s_page == PAGE_BOOT) {
        return;
    }
    s_overlay_return_screen = s_screen;
    s_overlay_return_page = s_page;
    s_overlay_return_bar = s_active_bar;
}

static void overlay_base(const char *title, const char *hint)
{
    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_overlay_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_overlay_bar;
    pp_page_title(s_screen, title);
    pp_footer(s_screen, hint);
    set_current(s_screen, PAGE_OVERLAY, &s_overlay_bar);
}

static void overlay_finish(void)
{
    lv_obj_t *screen = s_screen;
    const page_t return_page = s_overlay_return_page;
    lv_obj_t *return_screen = s_overlay_return_screen;
    pp_topbar_t *return_bar = s_overlay_return_bar;
    s_screen = NULL;
    s_active_bar = NULL;
    if (screen) lv_obj_delete(screen);
    free_overlay_images();
    s_overlay_return_screen = NULL;
    s_overlay_return_bar = NULL;
    if (return_screen) {
        s_screen = return_screen;
        s_page = return_page;
        s_active_bar = return_bar;
        if (return_bar) pp_topbar_update(return_bar, s_battery_soc);
        lv_screen_load(return_screen);
    } else {
        s_page = PAGE_NONE;
    }
}

static void show_uploaded_qr(const char *title, const char *rel,
                             const char *caption)
{
    remember_return_page();
    free_overlay_images();
    if (!s_storage || pp_image_load(s_storage, rel, &s_overlay_uploaded) !=
                          PP_STORAGE_OK) {
        overlay_base(title, "长按OK 返回");
        lv_obj_t *panel = pp_panel_create(s_screen, 12, 64, 216, 200);
        pp_icon_create(panel, PP_ICON_WARNING, PP_COLOR_WARN, 18, 18);
        dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 18, 46, 180, 120,
                  "二维码图片损坏或无法读取。请在管理后台重新上传。");
        return;
    }
    s_overlay_uploaded_loaded = true;

    overlay_base(title, "长按OK 返回");
    lv_obj_t *white = lv_obj_create(s_screen);
    lv_obj_set_pos(white, 16, 60);
    lv_obj_set_size(white, 208, 208);
    lv_obj_set_style_bg_color(white, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(white, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(white, 2, 0);
    lv_obj_set_style_border_color(white, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_radius(white, 0, 0);
    lv_obj_set_style_pad_all(white, 0, 0);
    lv_obj_remove_flag(white, LV_OBJ_FLAG_SCROLLABLE);
    pp_image_widget_create(white, &s_overlay_uploaded, 14, 14,
                           PP_IMAGE_QR_W, PP_IMAGE_QR_H);
    if (caption && caption[0]) {
        char clipped[192];
        pp_text_copy_display(clipped, sizeof(clipped), caption, 40);
        lv_obj_t *label = dyn_label(s_screen, &pp_font_12, PP_COLOR_MUTED,
                                    12, 274, 216, 18, clipped);
        if (label) {
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        }
    }
}

static void show_generated_qr(const char *title, const char *text,
                              const char *caption)
{
    remember_return_page();
    free_overlay_images();
    if (!pp_qr_render_text(text, &s_overlay_generated)) {
        overlay_base(title, "长按OK 返回");
        lv_obj_t *panel = pp_panel_create(s_screen, 12, 64, 216, 200);
        pp_icon_create(panel, PP_ICON_WARNING, PP_COLOR_WARN, 18, 18);
        dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 18, 46, 180, 120,
                  "内容过长或为空，无法生成二维码。请缩短链接后重试。");
        return;
    }
    s_overlay_generated_loaded = true;

    overlay_base(title, "长按OK 返回");
    lv_obj_t *white = lv_obj_create(s_screen);
    lv_obj_set_pos(white, 16, 60);
    lv_obj_set_size(white, 208, 208);
    lv_obj_set_style_bg_color(white, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(white, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(white, 2, 0);
    lv_obj_set_style_border_color(white, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_radius(white, 0, 0);
    lv_obj_set_style_pad_all(white, 0, 0);
    lv_obj_remove_flag(white, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *qr = lv_image_create(white);
    lv_image_set_src(qr, &s_overlay_generated.dsc);
    lv_obj_set_pos(qr, 14, 14);
    lv_obj_set_size(qr, PP_IMAGE_QR_W, PP_IMAGE_QR_H);
    lv_image_set_inner_align(qr, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(qr, false);
    lv_obj_set_style_image_recolor(qr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_image_recolor_opa(qr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(qr, LV_OBJ_FLAG_SCROLLABLE);

    if (caption && caption[0]) {
        char clipped[192];
        pp_text_copy_display(clipped, sizeof(clipped), caption, 40);
        lv_obj_t *label = dyn_label(s_screen, &pp_font_12, PP_COLOR_MUTED,
                                    12, 274, 216, 18, clipped);
        if (label) {
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        }
    }
}

static void show_text_overlay(const char *title, const char *body, bool large)
{
    remember_return_page();
    free_overlay_images();

    overlay_base(title, "长按OK 返回");
    lv_obj_t *panel = pp_panel_create(s_screen, 12, 64, 216, 200);
    char clipped[512];
    pp_text_copy_display(clipped, sizeof(clipped), body ? body : "", 160);
    lv_obj_t *label = dyn_label(panel, large ? &pp_font_24 : &pp_font_12,
                                PP_COLOR_TEXT, 12, large ? 60 : 16, 192,
                                large ? 80 : 168, clipped);
    if (label) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    }
}

static void show_notice(const char *title, const char *body)
{
    remember_return_page();
    free_overlay_images();

    overlay_base(title, "长按OK 返回");
    lv_obj_t *panel = pp_panel_create(s_screen, 12, 64, 216, 200);
    pp_icon_create(panel, PP_ICON_WARNING, PP_COLOR_WARN, 18, 18);
    dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 18, 48, 180, 130,
              body ? body : "");
}

/* ------------------------------------------------------------------------- */
/* Games                                                                     */
/* ------------------------------------------------------------------------- */

static game_entry_t s_entries[PP_GAMES_SAFETY_LIMIT + 4];
static size_t s_entry_count;
static size_t s_game_sel;
static size_t s_window_start;
static lv_obj_t *s_games_container;
static lv_obj_t *s_games_count;
static lv_obj_t *s_games_more_top;
static lv_obj_t *s_games_more_bottom;
static lv_obj_t *s_window_panels[GAME_WINDOW_ENTRIES];
static lv_obj_t *s_window_titles[GAME_WINDOW_ENTRIES];
static lv_obj_t *s_window_notes[GAME_WINDOW_ENTRIES];
static lv_obj_t *s_window_frames[GAME_WINDOW_ENTRIES];
static entry_kind_t s_window_kind[GAME_WINDOW_ENTRIES];
static size_t s_window_game_index[GAME_WINDOW_ENTRIES];
static cover_slot_t s_covers[GAME_COVER_SLOTS];

static void free_game_covers(void)
{
    for (int i = 0; i < GAME_COVER_SLOTS; ++i) {
        if (s_covers[i].loaded) {
            pp_image_free(&s_covers[i].image);
        }
        memset(&s_covers[i], 0, sizeof(s_covers[i]));
    }
}

static void build_game_entries(void)
{
    s_entry_count = 0;
    s_entries[s_entry_count++] = (game_entry_t){
        .kind = ENTRY_HEADER, .group = PP_GAME_GROUP_FAVORITE,
    };
    size_t favorites = 0;
    if (s_games) {
        for (size_t i = 0; i < s_games->count; ++i) {
            if (s_games->items[i].group != PP_GAME_GROUP_FAVORITE) continue;
            s_entries[s_entry_count++] = (game_entry_t){
                .kind = ENTRY_GAME,
                .group = PP_GAME_GROUP_FAVORITE,
                .game_index = i,
            };
            ++favorites;
        }
    }
    if (favorites == 0) {
        s_entries[s_entry_count++] = (game_entry_t){
            .kind = ENTRY_EMPTY, .group = PP_GAME_GROUP_FAVORITE,
        };
    }

    s_entries[s_entry_count++] = (game_entry_t){
        .kind = ENTRY_HEADER, .group = PP_GAME_GROUP_PLAYING,
    };
    size_t playing = 0;
    if (s_games) {
        for (size_t i = 0; i < s_games->count; ++i) {
            if (s_games->items[i].group != PP_GAME_GROUP_PLAYING) continue;
            s_entries[s_entry_count++] = (game_entry_t){
                .kind = ENTRY_GAME,
                .group = PP_GAME_GROUP_PLAYING,
                .game_index = i,
            };
            ++playing;
        }
    }
    if (playing == 0) {
        s_entries[s_entry_count++] = (game_entry_t){
            .kind = ENTRY_EMPTY, .group = PP_GAME_GROUP_PLAYING,
        };
    }
}

static size_t selected_entry_index(void)
{
    if (!s_games || s_games->count == 0) return 0;
    for (size_t i = 0; i < s_entry_count; ++i) {
        if (s_entries[i].kind == ENTRY_GAME &&
            s_entries[i].game_index == s_game_sel) {
            return i;
        }
    }
    return 0;
}

static void window_follow_selection(void)
{
    if (s_entry_count <= GAME_WINDOW_ENTRIES) {
        s_window_start = 0;
        return;
    }
    const size_t selected = selected_entry_index();
    if (selected < s_window_start) {
        s_window_start = selected;
    } else if (selected >= s_window_start + GAME_WINDOW_ENTRIES) {
        s_window_start = selected - GAME_WINDOW_ENTRIES + 1;
    }
    const size_t max_start = s_entry_count - GAME_WINDOW_ENTRIES;
    if (s_window_start > max_start) s_window_start = max_start;
}

static cover_slot_t *cover_find(size_t game_index)
{
    for (int i = 0; i < GAME_COVER_SLOTS; ++i) {
        if (s_covers[i].used && s_covers[i].game_index == game_index) {
            return &s_covers[i];
        }
    }
    return NULL;
}

static pp_loaded_image_t *cover_image(size_t game_index)
{
    cover_slot_t *slot = cover_find(game_index);
    return slot && slot->loaded ? &slot->image : NULL;
}

static void covers_prepare(const size_t *needed, size_t count)
{
    // Drop entries that left the window before loading new ones; the old row
    // widgets are already deleted, so no LVGL object references them.
    for (int i = 0; i < GAME_COVER_SLOTS; ++i) {
        if (!s_covers[i].used) continue;
        bool keep = false;
        for (size_t j = 0; j < count; ++j) {
            if (needed[j] == s_covers[i].game_index) {
                keep = true;
                break;
            }
        }
        if (!keep) {
            if (s_covers[i].loaded) pp_image_free(&s_covers[i].image);
            memset(&s_covers[i], 0, sizeof(s_covers[i]));
        }
    }
    for (size_t j = 0; j < count; ++j) {
        if (cover_find(needed[j])) continue;
        if (!s_games || needed[j] >= s_games->count) continue;
        cover_slot_t *slot = NULL;
        for (int i = 0; i < GAME_COVER_SLOTS; ++i) {
            if (!s_covers[i].used) {
                slot = &s_covers[i];
                break;
            }
        }
        if (!slot) continue;
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        slot->game_index = needed[j];
        slot->loaded = false;
        if (s_games->items[needed[j]].has_cover && s_storage) {
            char rel[PP_STORAGE_PATH_MAX];
            if (pp_image_cover_path(s_games->items[needed[j]].id, rel,
                                    sizeof(rel))) {
                slot->loaded =
                    pp_image_load(s_storage, rel, &slot->image) == PP_STORAGE_OK;
                if (!slot->loaded) {
                    ESP_LOGW(TAG, "封面加载失败: %s", rel);
                }
            }
        }
    }
}

static void games_refresh_selection(void)
{
    if (s_games_count && s_games) {
        char text[24];
        if (s_games->count == 0) {
            snprintf(text, sizeof(text), "0 / 0");
        } else {
            snprintf(text, sizeof(text), "%u / %u",
                     (unsigned)(s_game_sel + 1), (unsigned)s_games->count);
        }
        lv_label_set_text(s_games_count, text);
    }
    for (int i = 0; i < GAME_WINDOW_ENTRIES; ++i) {
        if (!s_window_panels[i] || s_window_kind[i] != ENTRY_GAME) continue;
        const bool selected = s_window_game_index[i] == s_game_sel;
        pp_row_style(s_window_panels[i], selected);
        pp_row_text_color(s_window_titles[i], selected);
        lv_obj_set_style_text_color(
            s_window_notes[i],
            lv_color_hex(selected ? PP_COLOR_MUTED : PP_COLOR_DIM), 0);
        if (s_window_frames[i]) {
            lv_obj_set_style_border_color(
                s_window_frames[i],
                lv_color_hex(selected ? PP_COLOR_MINT : PP_COLOR_BORDER), 0);
        }
    }
}

static void games_render_window(void)
{
    if (s_games_container) {
        lv_obj_delete(s_games_container);
        s_games_container = NULL;
    }
    for (int i = 0; i < GAME_WINDOW_ENTRIES; ++i) {
        s_window_panels[i] = NULL;
        s_window_titles[i] = NULL;
        s_window_notes[i] = NULL;
        s_window_frames[i] = NULL;
        s_window_kind[i] = ENTRY_EMPTY;
        s_window_game_index[i] = SIZE_MAX;
    }

    size_t needed[GAME_WINDOW_ENTRIES];
    size_t needed_count = 0;
    const size_t end = s_window_start + GAME_WINDOW_ENTRIES < s_entry_count
                           ? s_window_start + GAME_WINDOW_ENTRIES
                           : s_entry_count;
    for (size_t i = s_window_start; i < end; ++i) {
        if (s_entries[i].kind != ENTRY_GAME) continue;
        needed[needed_count++] = s_entries[i].game_index;
    }
    covers_prepare(needed, needed_count);

    s_games_container = lv_obj_create(s_screen);
    lv_obj_set_pos(s_games_container, 12, 58);
    lv_obj_set_size(s_games_container, 216, 232);
    lv_obj_set_style_bg_opa(s_games_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_games_container, 0, 0);
    lv_obj_set_style_pad_all(s_games_container, 0, 0);
    lv_obj_set_style_radius(s_games_container, 0, 0);
    lv_obj_remove_flag(s_games_container, LV_OBJ_FLAG_SCROLLABLE);

    int y = 0;
    int slot = 0;
    for (size_t i = s_window_start; i < end && slot < GAME_WINDOW_ENTRIES;
         ++i, ++slot) {
        const game_entry_t *entry = &s_entries[i];
        s_window_kind[slot] = entry->kind;
        if (entry->kind == ENTRY_HEADER) {
            lv_obj_t *accent = lv_obj_create(s_games_container);
            lv_obj_set_pos(accent, 0, y + 4);
            lv_obj_set_size(accent, 3, 14);
            lv_obj_set_style_bg_color(accent, lv_color_hex(PP_COLOR_MINT), 0);
            lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(accent, 0, 0);
            lv_obj_set_style_radius(accent, 0, 0);
            lv_obj_set_style_pad_all(accent, 0, 0);
            lv_obj_remove_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

            pp_icon_create(s_games_container,
                           entry->group == PP_GAME_GROUP_FAVORITE
                               ? PP_ICON_HEART
                               : PP_ICON_PLAY,
                           PP_COLOR_MINT, 8, y + 3);
            const char *title = entry->group == PP_GAME_GROUP_FAVORITE
                                    ? "喜欢的游戏"
                                    : "正在玩的游戏";
            pp_label_create(s_games_container, title, &pp_font_12,
                            PP_COLOR_MUTED, 28, y + 2, 160);
            y += 22;
            continue;
        }
        if (entry->kind == ENTRY_EMPTY) {
            pp_label_create(s_games_container, "暂无游戏", &pp_font_12,
                            PP_COLOR_DIM, 28, y + 3, 160);
            y += 22;
            continue;
        }

        const size_t game_index = entry->game_index;
        const pp_game_t *game =
            s_games && game_index < s_games->count ? &s_games->items[game_index]
                                                   : NULL;
        if (!game) continue;
        s_window_game_index[slot] = game_index;
        lv_obj_t *panel = pp_panel_create(s_games_container, 0, y, 216, 44);
        s_window_panels[slot] = panel;

        lv_obj_t *frame = pp_panel_create(panel, 4, 1, 32, 42);
        s_window_frames[slot] = frame;
        pp_loaded_image_t *cover = cover_image(game_index);
        if (cover) {
            pp_image_widget_create(frame, cover, 1, 1, 30, 40);
        } else {
            pp_icon_create(frame, PP_ICON_IMAGE, PP_COLOR_DIM, 9, 14);
        }

        s_window_titles[slot] = dyn_label(panel, &pp_font_12, PP_COLOR_TEXT,
                                          42, 3, 150, 16,
                                          game->title[0] ? game->title
                                                         : "未命名游戏");
        if (s_window_titles[slot]) {
            lv_label_set_long_mode(s_window_titles[slot],
                                   LV_LABEL_LONG_MODE_DOTS);
        }
        s_window_notes[slot] = dyn_label(
            panel, &pp_font_12, PP_COLOR_DIM, 42, 21, 150, 16,
            game->note[0] ? game->note : "—");
        if (s_window_notes[slot]) {
            lv_label_set_long_mode(s_window_notes[slot],
                                   LV_LABEL_LONG_MODE_DOTS);
        }
        pp_icon_create(panel, PP_ICON_CHEVRON, PP_COLOR_DIM, 198, 16);
        y += 44;
    }

    const bool more_top = s_window_start > 0;
    const bool more_bottom = s_window_start + GAME_WINDOW_ENTRIES < s_entry_count;
    if (s_games_more_top) {
        lv_obj_set_style_bg_opa(s_games_more_top,
                                more_top ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    if (s_games_more_bottom) {
        lv_obj_set_style_bg_opa(
            s_games_more_bottom, more_bottom ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    games_refresh_selection();
}

static void build_games(void)
{
    destroy_all_pages();
    if (s_games && s_games->count > 0 && s_game_sel >= s_games->count) {
        s_game_sel = s_games->count - 1;
    }
    build_game_entries();
    s_window_start = 0;
    window_follow_selection();

    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_games_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_games_bar;
    pp_page_title(s_screen, "游戏收藏");
    s_games_count = pp_label_create(s_screen, "0 / 0", &pp_font_12,
                                    PP_COLOR_MINT, 160, 34, 68);
    if (s_games_count) {
        lv_obj_set_style_text_align(s_games_count, LV_TEXT_ALIGN_RIGHT, 0);
    }

    s_games_more_top = lv_obj_create(s_screen);
    lv_obj_set_pos(s_games_more_top, 12, 56);
    lv_obj_set_size(s_games_more_top, 216, 2);
    lv_obj_set_style_bg_color(s_games_more_top, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(s_games_more_top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_games_more_top, 0, 0);
    lv_obj_set_style_radius(s_games_more_top, 0, 0);
    lv_obj_set_style_pad_all(s_games_more_top, 0, 0);
    lv_obj_remove_flag(s_games_more_top, LV_OBJ_FLAG_SCROLLABLE);

    s_games_more_bottom = lv_obj_create(s_screen);
    lv_obj_set_pos(s_games_more_bottom, 12, 290);
    lv_obj_set_size(s_games_more_bottom, 216, 2);
    lv_obj_set_style_bg_color(s_games_more_bottom,
                              lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(s_games_more_bottom, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_games_more_bottom, 0, 0);
    lv_obj_set_style_radius(s_games_more_bottom, 0, 0);
    lv_obj_set_style_pad_all(s_games_more_bottom, 0, 0);
    lv_obj_remove_flag(s_games_more_bottom, LV_OBJ_FLAG_SCROLLABLE);

    pp_footer(s_screen, "OK 查看 · 长按OK 返回");
    set_current(s_screen, PAGE_GAMES, &s_games_bar);
    games_render_window();
}

static void games_move(int delta)
{
    if (!s_games || s_games->count == 0) return;
    size_t next = s_game_sel;
    if (delta < 0) {
        if (next > 0) --next;
    } else {
        if (next + 1 < s_games->count) ++next;
    }
    if (next == s_game_sel) return;
    s_game_sel = next;
    const size_t old_start = s_window_start;
    window_follow_selection();
    if (old_start != s_window_start) {
        games_render_window();
    } else {
        games_refresh_selection();
    }
}

/* ------------------------------------------------------------------------- */
/* Game detail                                                               */
/* ------------------------------------------------------------------------- */

static size_t s_detail_index;
static pp_loaded_image_t s_detail_cover;
static bool s_detail_cover_loaded;

static void free_detail_cover(void)
{
    if (s_detail_cover_loaded) {
        pp_image_free(&s_detail_cover);
        s_detail_cover_loaded = false;
    }
}

static void build_detail(size_t index)
{
    destroy_all_pages();
    s_detail_index = index;
    const pp_game_t *game = s_games && index < s_games->count
                                ? &s_games->items[index]
                                : NULL;
    if (!game) return;

    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_detail_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_detail_bar;
    pp_page_title(s_screen, "游戏详情");

    lv_obj_t *frame = pp_panel_create(s_screen, 12, 60, 76, 100);
    if (game->has_cover && s_storage) {
        char rel[PP_STORAGE_PATH_MAX];
        if (pp_image_cover_path(game->id, rel, sizeof(rel)) &&
            pp_image_load(s_storage, rel, &s_detail_cover) == PP_STORAGE_OK) {
            s_detail_cover_loaded = true;
            pp_image_widget_create(frame, &s_detail_cover, 2, 2,
                                   PP_IMAGE_COVER_W, PP_IMAGE_COVER_H);
        }
    }
    if (!s_detail_cover_loaded) {
        lv_obj_t *placeholder = pp_icon_create(frame, PP_ICON_IMAGE,
                                               PP_COLOR_DIM, 24, 36);
        if (placeholder) lv_obj_set_size(placeholder, 28, 28);
    }

    lv_obj_t *title = dyn_label(s_screen, &pp_font_24, PP_COLOR_TEXT, 96, 58,
                                132, 56, game->title);
    if (title) lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *badge_bar = lv_obj_create(s_screen);
    lv_obj_set_pos(badge_bar, 96, 121);
    lv_obj_set_size(badge_bar, 3, 12);
    lv_obj_set_style_bg_color(badge_bar, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(badge_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(badge_bar, 0, 0);
    lv_obj_set_style_radius(badge_bar, 0, 0);
    lv_obj_set_style_pad_all(badge_bar, 0, 0);
    lv_obj_remove_flag(badge_bar, LV_OBJ_FLAG_SCROLLABLE);

    pp_label_create(s_screen,
                    game->group == PP_GAME_GROUP_FAVORITE ? "喜欢的游戏"
                                                          : "正在玩的游戏",
                    &pp_font_12, PP_COLOR_MINT, 103, 119, 120);

    pp_label_create(s_screen, "小黑盒链接", &pp_font_12, PP_COLOR_DIM, 96,
                    142, 120);
    pp_label_create(s_screen, game->link[0] ? "已设置" : "未设置", &pp_font_12,
                    game->link[0] ? PP_COLOR_MINT : PP_COLOR_DIM, 96, 158,
                    120);

    lv_obj_t *intro_panel = pp_panel_create(s_screen, 12, 176, 216, 72);
    pp_label_create(intro_panel, "简介", &pp_font_12, PP_COLOR_MINT, 8, 6, 60);
    lv_obj_t *intro = dyn_label(intro_panel, &pp_font_12, PP_COLOR_MUTED, 8, 24,
                                200, 48,
                                game->note[0] ? game->note : "暂无介绍");
    if (intro) lv_label_set_long_mode(intro, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *link_row = pp_panel_create(s_screen, 12, 256, 216, 26);
    pp_icon_create(link_row, PP_ICON_LINK,
                   game->link[0] ? PP_COLOR_MINT : PP_COLOR_DIM, 6, 6);
    pp_label_create(link_row,
                    game->link[0] ? "小黑盒社区入口" : "未设置小黑盒链接",
                    &pp_font_12,
                    game->link[0] ? PP_COLOR_TEXT : PP_COLOR_DIM, 26, 5, 160);
    if (game->link[0]) {
        pp_icon_create(link_row, PP_ICON_CHEVRON, PP_COLOR_MINT, 198, 7);
    }

    pp_footer(s_screen, "OK 显示二维码 · 长按OK 返回");
    set_current(s_screen, PAGE_DETAIL, &s_detail_bar);
}

/* ------------------------------------------------------------------------- */
/* Contact                                                                   */
/* ------------------------------------------------------------------------- */

#define CONTACT_ROWS 5
static int s_contact_sel;
static lv_obj_t *s_contact_panel[CONTACT_ROWS];
static lv_obj_t *s_contact_label[CONTACT_ROWS];
static lv_obj_t *s_contact_value[CONTACT_ROWS];
static lv_obj_t *s_contact_icon[CONTACT_ROWS];
static lv_obj_t *s_contact_chevron[CONTACT_ROWS];

static const char *contact_value(int index)
{
    if (!s_profile || !s_images) return "";
    switch (index) {
    case 0:
        if (s_profile->weibo_handle[0]) return s_profile->weibo_handle;
        if (s_profile->weibo_url[0]) return "已设置链接";
        return "未设置";
    case 1:
        return s_profile->wechat_name[0] ? s_profile->wechat_name : "未设置";
    case 2:
        return s_profile->wechat_id[0] ? s_profile->wechat_id : "未设置";
    case 3:
        return s_images->wechat_qr ? "已上传" : "未上传";
    case 4:
        return s_images->wechat_pay_qr ? "已上传" : "未上传";
    default:
        return "";
    }
}

static void contact_refresh(void)
{
    for (int i = 0; i < CONTACT_ROWS; ++i) {
        const bool selected = s_contact_sel == i;
        pp_row_style(s_contact_panel[i], selected);
        pp_row_text_color(s_contact_label[i], selected);
        const char *value = contact_value(i);
        const bool placeholder = strcmp(value, "未设置") == 0 ||
                                 strcmp(value, "未上传") == 0;
        lv_obj_set_style_text_color(
            s_contact_value[i],
            lv_color_hex(selected ? PP_COLOR_MINT
                                  : (placeholder ? PP_COLOR_DIM
                                                 : PP_COLOR_TEXT)),
            0);
        pp_icon_recolor(s_contact_icon[i],
                        selected ? PP_COLOR_MINT : PP_COLOR_MUTED);
        pp_icon_recolor(s_contact_chevron[i],
                        selected ? PP_COLOR_MINT : PP_COLOR_DIM);
    }
}

static void build_contact(void)
{
    destroy_all_pages();
    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_contact_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_contact_bar;
    pp_page_title(s_screen, "联系方式");

    static const pp_icon_id_t icons[CONTACT_ROWS] = {
        PP_ICON_EYE, PP_ICON_BUBBLE, PP_ICON_CHAT, PP_ICON_QR, PP_ICON_QR,
    };
    static const char *const labels[CONTACT_ROWS] = {
        "微博", "微信", "微信号", "微信二维码", "微信收款码",
    };
    for (int i = 0; i < CONTACT_ROWS; ++i) {
        lv_obj_t *panel = pp_panel_create(s_screen, 12, 64 + i * 44, 216, 38);
        s_contact_panel[i] = panel;
        s_contact_icon[i] = pp_icon_create(panel, icons[i], PP_COLOR_MUTED,
                                           5, 12);
        s_contact_label[i] = pp_label_create(panel, labels[i], &pp_font_12,
                                             PP_COLOR_TEXT, 26, 11, 66);
        s_contact_value[i] = dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 92, 11,
                                       100, 16, contact_value(i));
        if (s_contact_value[i]) {
            lv_obj_set_style_text_align(s_contact_value[i],
                                        LV_TEXT_ALIGN_RIGHT, 0);
            lv_label_set_long_mode(s_contact_value[i], LV_LABEL_LONG_MODE_DOTS);
        }
        s_contact_chevron[i] = pp_icon_create(panel, PP_ICON_CHEVRON,
                                              PP_COLOR_DIM, 198, 13);
        if (s_contact_chevron[i]) {
            lv_obj_set_size(s_contact_chevron[i], 12, 12);
        }
    }
    pp_footer(s_screen, "OK 查看 · 长按OK 返回");
    set_current(s_screen, PAGE_CONTACT, &s_contact_bar);
    contact_refresh();
}

static void contact_activate(int index)
{
    if (!s_profile || !s_images) return;
    char rel[PP_STORAGE_PATH_MAX];
    switch (index) {
    case 0:
        if (s_profile->weibo_url[0]) {
            show_generated_qr("微博二维码", s_profile->weibo_url,
                              s_profile->weibo_handle[0]
                                  ? s_profile->weibo_handle
                                  : s_profile->weibo_url);
        } else if (s_profile->weibo_handle[0]) {
            char body[192];
            snprintf(body, sizeof(body), "%s%s",
                     s_profile->weibo_handle[0] == '@' ? "" : "@",
                     s_profile->weibo_handle);
            show_text_overlay("微博", body, true);
        } else {
            show_notice("微博", "还没有设置微博账号。请在管理后台的联系方式页面填写。");
        }
        break;
    case 1:
        if (s_profile->wechat_name[0]) {
            show_text_overlay("微信", s_profile->wechat_name, true);
        } else {
            show_notice("微信", "还没有设置微信昵称。请在管理后台补充。");
        }
        break;
    case 2:
        if (s_profile->wechat_id[0]) {
            show_text_overlay("微信号", s_profile->wechat_id, true);
        } else {
            show_notice("微信号", "还没有设置微信号。请在管理后台补充。");
        }
        break;
    case 3:
        if (s_images->wechat_qr && s_storage &&
            pp_image_profile_qr_path(PP_IMAGE_SLOT_QR_WECHAT, rel, sizeof(rel))) {
            show_uploaded_qr("微信二维码", rel, "扫码添加微信");
        } else {
            show_notice("微信二维码", "还没有上传微信二维码。请在管理后台的联系方式页面上传。");
        }
        break;
    case 4:
        if (s_images->wechat_pay_qr && s_storage &&
            pp_image_profile_qr_path(PP_IMAGE_SLOT_QR_WECHAT_PAY, rel,
                                     sizeof(rel))) {
            show_uploaded_qr("微信收款码", rel, "扫码向我付款");
        } else {
            show_notice("微信收款码", "还没有上传微信收款二维码。请在管理后台的联系方式页面上传。");
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* NFC                                                                       */
/* ------------------------------------------------------------------------- */

static void build_nfc(void)
{
    destroy_all_pages();
    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_nfc_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_nfc_bar;
    pp_page_title(s_screen, "NFC 名片");

    const bool enabled = s_nfc && s_nfc->enabled;
    lv_obj_t *panel = pp_panel_create(s_screen, 12, 60, 216, 190);
    if (!enabled) {
        pp_icon_create(panel, PP_ICON_TAG, PP_COLOR_DIM, 18, 18);
        pp_label_create(panel, "未启用 NFC 入口", &pp_font_24, PP_COLOR_DIM,
                        18, 46, 180);
        dyn_label(panel, &pp_font_12, PP_COLOR_MUTED, 18, 92, 180, 70,
                  "在管理后台的 NFC 页面填写入口内容。设备会把内容显示在这里，"
                  "供你写入卡片上的 NTAG213 标签。");
    } else {
        dyn_label(panel, &pp_font_24, PP_COLOR_TEXT, 12, 10, 192, 32,
                  s_nfc->label);
        pp_label_create(panel,
                        s_nfc->type == PP_NFC_TYPE_URL ? "网址入口" : "文本入口",
                        &pp_font_12, PP_COLOR_MINT, 12, 46, 100);
        lv_obj_t *content = dyn_label(panel, &pp_font_12, PP_COLOR_MUTED, 12,
                                      68, 192, 78, s_nfc->content);
        if (content) lv_label_set_long_mode(content, LV_LABEL_LONG_MODE_DOTS);
        dyn_label(panel, &pp_font_12, PP_COLOR_DIM, 12, 152, 192, 30,
                  "把以上内容写入卡片上的 NTAG213 标签，手机靠近即可打开。");
    }
    pp_footer(s_screen,
              enabled ? (s_nfc->type == PP_NFC_TYPE_URL ? "OK 显示二维码 · 长按OK 返回"
                                                        : "OK 放大查看 · 长按OK 返回")
                      : "长按OK 返回");
    set_current(s_screen, PAGE_NFC, &s_nfc_bar);
}

static void nfc_activate(void)
{
    if (!s_nfc || !s_nfc->enabled) {
        show_notice("NFC 名片", "NFC 入口尚未启用。请先在管理后台配置内容。");
        return;
    }
    if (s_nfc->type == PP_NFC_TYPE_URL) {
        show_generated_qr("NFC 入口", s_nfc->content, s_nfc->label);
    } else {
        show_text_overlay("NFC 文本", s_nfc->content, false);
    }
}

/* ------------------------------------------------------------------------- */
/* Boot, error, management                                                   */
/* ------------------------------------------------------------------------- */

void ui_app_show_boot(void)
{
    destroy_all_pages();
    s_screen = pp_screen_create();
    if (!s_screen) return;
    lv_obj_t *brand = pp_label_create(s_screen, "PLAYER PASS", &pp_font_24,
                                      PP_COLOR_TEXT, 12, 126, 216);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *bar = lv_obj_create(s_screen);
    lv_obj_set_pos(bar, 96, 166);
    lv_obj_set_size(bar, 48, 4);
    lv_obj_set_style_bg_color(bar, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *sub = pp_label_create(s_screen, "个人像素名片", &pp_font_12,
                                    PP_COLOR_MUTED, 12, 184, 216);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *loading = pp_label_create(s_screen, "正在载入", &pp_font_12,
                                        PP_COLOR_DIM, 12, 276, 216);
    lv_obj_set_style_text_align(loading, LV_TEXT_ALIGN_CENTER, 0);
    set_current(s_screen, PAGE_BOOT, NULL);
}

void ui_app_show_error(const char *title, const char *body)
{
    destroy_all_pages();
    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_error_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_error_bar;
    pp_page_title(s_screen, title);
    lv_obj_t *panel = pp_panel_create(s_screen, 12, 64, 216, 200);
    pp_icon_create(panel, PP_ICON_WARNING, PP_COLOR_ERROR, 18, 18);
    lv_obj_t *text = dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 18, 48, 180,
                               130, body);
    if (text) lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    pp_footer(s_screen, "长按OK 重启设备");
    set_current(s_screen, PAGE_ERROR, NULL);
}

void ui_app_show_startup_notice(const char *title, const char *body)
{
    ui_app_show_home();
    show_notice(title, body);
}

static lv_obj_t *s_manage_status;
static lv_obj_t *s_manage_ssid;
static lv_obj_t *s_manage_password;
static lv_obj_t *s_manage_url;
static lv_obj_t *s_manage_storage_text;
static lv_obj_t *s_manage_storage_fill;
static lv_obj_t *s_manage_footer;
static lv_obj_t *s_manage_dot;
static pp_qr_image_t s_manage_qr;
static bool s_manage_qr_loaded;

static void free_manage_qr(void)
{
    if (s_manage_qr_loaded) {
        pp_qr_free(&s_manage_qr);
        s_manage_qr_loaded = false;
    }
}

static const char *storage_text(char *buffer, size_t size, const ui_manage_info_t *info)
{
    const uint64_t used_mb10 = (info->used_bytes * 10u + (1u << 19)) >> 20;
    const uint64_t total_mb10 = (info->total_bytes * 10u + (1u << 19)) >> 20;
    snprintf(buffer, size, "已用 %d%% · %llu.%llu / %llu.%llu MB",
             info->storage_percent, (unsigned long long)(used_mb10 / 10),
             (unsigned long long)(used_mb10 % 10),
             (unsigned long long)(total_mb10 / 10),
             (unsigned long long)(total_mb10 % 10));
    return buffer;
}

void ui_app_show_manage(const ui_manage_info_t *info)
{
    destroy_all_pages();
    s_screen = pp_screen_create();
    if (!s_screen) return;
    s_manage_bar = pp_topbar_create(s_screen);
    s_active_bar = &s_manage_bar;
    pp_page_title(s_screen, "管理模式");

    s_manage_dot = pp_icon_create(s_screen, PP_ICON_DOT, PP_COLOR_DIM, 12, 64);
    if (s_manage_dot) lv_obj_set_size(s_manage_dot, 10, 10);
    s_manage_status = pp_label_create(s_screen, "", &pp_font_12, PP_COLOR_MUTED,
                                      28, 60, 200);
    if (s_manage_status) {
        lv_label_set_long_mode(s_manage_status, LV_LABEL_LONG_MODE_DOTS);
    }

    // White QR tile keeps the management URL scannable even though the rest of
    // the screen is dark.  The QR is regenerated whenever this page is rebuilt.
    lv_obj_t *qr_panel = lv_obj_create(s_screen);
    lv_obj_set_pos(qr_panel, 66, 78);
    lv_obj_set_size(qr_panel, 108, 108);
    lv_obj_set_style_bg_color(qr_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(qr_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(qr_panel, 2, 0);
    lv_obj_set_style_border_color(qr_panel, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_radius(qr_panel, 0, 0);
    lv_obj_set_style_pad_all(qr_panel, 0, 0);
    lv_obj_remove_flag(qr_panel, LV_OBJ_FLAG_SCROLLABLE);

    bool qr_ok = false;
    if (info && info->url[0]) {
        qr_ok = pp_qr_render_text_sized(info->url, 100, &s_manage_qr);
        s_manage_qr_loaded = qr_ok;
    }
    if (qr_ok) {
        lv_obj_t *qr = lv_image_create(qr_panel);
        lv_image_set_src(qr, &s_manage_qr.dsc);
        lv_obj_set_pos(qr, 4, 4);
        lv_obj_set_size(qr, 100, 100);
        lv_image_set_inner_align(qr, LV_IMAGE_ALIGN_STRETCH);
        lv_image_set_antialias(qr, false);
        lv_obj_set_style_image_recolor(qr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_image_recolor_opa(qr, LV_OPA_COVER, 0);
        lv_obj_remove_flag(qr, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        pp_icon_create(qr_panel, PP_ICON_QR, PP_COLOR_PANEL, 40, 30);
        lv_obj_t *hint = pp_label_create(
            qr_panel,
            (info && info->state == UI_MANAGE_READY) ? "生成失败"
                                                     : "热点启动后显示",
            &pp_font_12, PP_COLOR_PANEL, 4, 70, 100);
        if (hint) {
            lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_DOTS);
        }
    }

    s_manage_url = pp_label_create(s_screen, "", &pp_font_12, PP_COLOR_MINT,
                                   12, 190, 216);
    if (s_manage_url) {
        lv_obj_set_style_text_align(s_manage_url, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_manage_url, LV_LABEL_LONG_MODE_DOTS);
    }

    lv_obj_t *panel = pp_panel_create(s_screen, 12, 212, 216, 66);
    pp_label_create(panel, "热点名称", &pp_font_12, PP_COLOR_DIM, 8, 6, 60);
    s_manage_ssid = dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 70, 6, 138, 16,
                              "");
    pp_label_create(panel, "热点密码", &pp_font_12, PP_COLOR_DIM, 8, 24, 60);
    s_manage_password = dyn_label(panel, &pp_font_12, PP_COLOR_TEXT, 70, 24,
                                  138, 16, "");
    pp_label_create(panel, "存储空间", &pp_font_12, PP_COLOR_DIM, 8, 42, 60);
    s_manage_storage_text = dyn_label(panel, &pp_font_12, PP_COLOR_MUTED, 70,
                                      42, 138, 16, "");
    s_manage_storage_fill = pp_progress_create(panel, 8, 58, 200, 6,
                                               PP_COLOR_MINT);
    if (s_manage_ssid) {
        lv_obj_set_style_text_align(s_manage_ssid, LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(s_manage_ssid, LV_LABEL_LONG_MODE_DOTS);
    }
    if (s_manage_password) {
        lv_obj_set_style_text_align(s_manage_password, LV_TEXT_ALIGN_RIGHT, 0);
    }
    if (s_manage_storage_text) {
        lv_obj_set_style_text_align(s_manage_storage_text, LV_TEXT_ALIGN_RIGHT,
                                    0);
        lv_label_set_long_mode(s_manage_storage_text, LV_LABEL_LONG_MODE_DOTS);
    }

    s_manage_footer = pp_label_create(s_screen, "", &pp_font_12, PP_COLOR_DIM,
                                      12, 302, 216);
    lv_obj_set_style_text_align(s_manage_footer, LV_TEXT_ALIGN_CENTER, 0);
    pp_divider(s_screen, 298);

    set_current(s_screen, PAGE_MANAGE, &s_manage_bar);
    ui_app_update_manage(info);
}

void ui_app_update_manage(const ui_manage_info_t *info)
{
    if (!info || s_page != PAGE_MANAGE) return;
    const bool ready = info->state == UI_MANAGE_READY;
    const bool failed = info->state == UI_MANAGE_FAILED;
    const uint32_t status_color =
        failed ? PP_COLOR_ERROR : (ready ? PP_COLOR_MINT : PP_COLOR_WARN);
    if (s_manage_dot) pp_icon_recolor(s_manage_dot, status_color);
    if (s_manage_status) {
        char text[224];
        if (failed) {
            snprintf(text, sizeof(text), "启动失败：%s",
                     info->error[0] ? info->error : "请查看串口日志");
        } else if (!ready) {
            snprintf(text, sizeof(text), "正在开启热点…");
        } else if (info->lease_active) {
            snprintf(text, sizeof(text), "热点已开启 · 编辑中：%s",
                     info->client_name[0] ? info->client_name : "浏览器");
        } else if (info->reserved) {
            snprintf(text, sizeof(text), "热点已开启 · 等待原客户端重连");
        } else if (info->client_connected) {
            snprintf(text, sizeof(text), "热点已开启 · 已连接，等待编辑权限");
        } else {
            snprintf(text, sizeof(text), "热点已开启 · 等待浏览器连接");
        }
        set_text(s_manage_status, text);
        lv_obj_set_style_text_color(s_manage_status,
                                    lv_color_hex(status_color), 0);
    }
    if (s_manage_ssid) set_text(s_manage_ssid, info->ssid);
    if (s_manage_password) set_text(s_manage_password, info->password);
    if (s_manage_url) set_text(s_manage_url, info->url);

    if (s_manage_storage_text) {
        char text[96];
        set_text(s_manage_storage_text,
                 storage_text(text, sizeof(text), info));
    }
    if (s_manage_storage_fill) {
        const uint32_t color =
            info->storage_level >= 2
                ? PP_COLOR_ERROR
                : (info->storage_level == 1 ? PP_COLOR_WARN : PP_COLOR_MINT);
        lv_obj_set_style_bg_color(s_manage_storage_fill, lv_color_hex(color), 0);
        pp_progress_set(s_manage_storage_fill, info->storage_percent);
    }
    if (s_manage_footer) {
        set_text(s_manage_footer, failed ? "长按OK 返回" : "长按OK 退出");
    }
}

/* ------------------------------------------------------------------------- */
/* Public state and keys                                                     */
/* ------------------------------------------------------------------------- */

void ui_app_set_data(const pp_storage_t *storage, const pp_profile_t *profile,
                     const pp_profile_images_t *images, const pp_games_t *games,
                     const pp_nfc_t *nfc)
{
    s_storage = storage;
    s_profile = profile;
    s_images = images;
    s_games = games;
    s_nfc = nfc;
}

void ui_app_set_battery(int soc_percent)
{
    s_battery_soc = soc_percent;
    if (s_active_bar) pp_topbar_update(s_active_bar, soc_percent);
}

void ui_app_show_home(void)
{
    build_home();
}

ui_action_t ui_app_handle_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (button == BSP_BTN_OK && event == BSP_BTN_LONG) {
        switch (s_page) {
        case PAGE_OVERLAY:
            overlay_finish();
            return UI_ACTION_NONE;
        case PAGE_GAMES:
        case PAGE_CONTACT:
        case PAGE_NFC:
            build_home();
            return UI_ACTION_NONE;
        case PAGE_DETAIL:
            build_games();
            return UI_ACTION_NONE;
        case PAGE_MANAGE:
            return UI_ACTION_EXIT_MANAGE;
        case PAGE_ERROR:
            return UI_ACTION_RESTART;
        default:
            return UI_ACTION_NONE;
        }
    }

    if (event == BSP_BTN_DOUBLE) {
        if (button == BSP_BTN_OK && s_page == PAGE_HOME) {
            return UI_ACTION_ENTER_MANAGE;
        }
        return UI_ACTION_NONE;
    }

    if (event != BSP_BTN_CLICK) return UI_ACTION_NONE;

    switch (s_page) {
    case PAGE_HOME:
        if (button == BSP_BTN_UP) {
            if (s_home_sel > 0) --s_home_sel;
            home_refresh();
        } else if (button == BSP_BTN_DOWN) {
            if (s_home_sel + 1 < HOME_ITEMS) ++s_home_sel;
            home_refresh();
        } else if (button == BSP_BTN_OK) {
            if (s_home_sel < HOME_PLATFORMS) {
                const int index = s_home_sel;
                char rel[PP_STORAGE_PATH_MAX];
                static const char *const titles[HOME_PLATFORMS] = {
                    "Steam 二维码", "Switch 二维码", "PlayStation 二维码",
                };
                if (!platform_has_qr(index)) {
                    char body[128];
                    snprintf(body, sizeof(body),
                             "还没有上传 %s 二维码。请到管理后台的“个人资料”"
                             "页面上传。",
                             PLATFORM_NAMES[index]);
                    show_notice(titles[index], body);
                } else if (pp_image_profile_qr_path(
                               index == 0   ? PP_IMAGE_SLOT_QR_STEAM
                               : index == 1 ? PP_IMAGE_SLOT_QR_SWITCH
                                            : PP_IMAGE_SLOT_QR_PSN,
                               rel, sizeof(rel))) {
                    char caption[96];
                    const char *id = platform_id(index);
                    snprintf(caption, sizeof(caption), "%s",
                             id[0] ? id : "扫码添加好友");
                    show_uploaded_qr(titles[index], rel, caption);
                }
            } else {
                const int nav = s_home_sel - HOME_PLATFORMS;
                if (nav == 0) build_games();
                else if (nav == 1) build_contact();
                else build_nfc();
            }
        }
        break;
    case PAGE_GAMES:
        if (!s_games || s_games->count == 0) break;
        if (button == BSP_BTN_UP) {
            games_move(-1);
        } else if (button == BSP_BTN_DOWN) {
            games_move(1);
        } else if (button == BSP_BTN_OK) {
            build_detail(s_game_sel);
        }
        break;
    case PAGE_DETAIL:
        if (button == BSP_BTN_OK) {
            const pp_game_t *game =
                s_games && s_detail_index < s_games->count
                    ? &s_games->items[s_detail_index]
                    : NULL;
            if (game && game->link[0]) {
                show_generated_qr("小黑盒入口", game->link, game->link);
            } else {
                show_notice("小黑盒入口",
                            "这款游戏还没有设置小黑盒链接。请在管理后台补充。");
            }
        }
        break;
    case PAGE_CONTACT:
        if (button == BSP_BTN_UP) {
            if (s_contact_sel > 0) --s_contact_sel;
            contact_refresh();
        } else if (button == BSP_BTN_DOWN) {
            if (s_contact_sel + 1 < CONTACT_ROWS) ++s_contact_sel;
            contact_refresh();
        } else if (button == BSP_BTN_OK) {
            contact_activate(s_contact_sel);
        }
        break;
    case PAGE_NFC:
        if (button == BSP_BTN_OK) nfc_activate();
        break;
    case PAGE_OVERLAY:
        if (button == BSP_BTN_OK) overlay_finish();
        break;
    default:
        break;
    }
    return UI_ACTION_NONE;
}
