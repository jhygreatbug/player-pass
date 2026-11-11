// Shared visual language for the device UI.
//
// Deep blue-gray background, mint accent, low-saturation borders, hard pixel
// edges.  Every screen is built from these helpers so spacing and selection
// feedback stay consistent.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui/icons.h"

#define PP_COLOR_BG 0x0B1220u
#define PP_COLOR_PANEL 0x141E30u
#define PP_COLOR_PANEL_ALT 0x1B2B3Fu
#define PP_COLOR_BORDER 0x2A3A55u
#define PP_COLOR_MINT 0x5EEAD4u
#define PP_COLOR_MINT_DARK 0x0F766Eu
#define PP_COLOR_TEXT 0xE6EEF8u
#define PP_COLOR_MUTED 0x8CA3C0u
#define PP_COLOR_DIM 0x5B708Fu
#define PP_COLOR_WARN 0xFBBF24u
#define PP_COLOR_ERROR 0xF87171u

#define PP_SCREEN_W 240
#define PP_SCREEN_H 320
#define PP_TOPBAR_H 24
#define PP_FOOTER_Y 300

typedef struct {
    lv_obj_t *root;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_text;
} pp_topbar_t;

lv_obj_t *pp_screen_create(void);

// Top bar: brand on the left, battery on the right.  Call the update function
// whenever a fresh SOC reading arrives; -1 renders "--" without a fill.
pp_topbar_t pp_topbar_create(lv_obj_t *screen);
void pp_topbar_update(pp_topbar_t *bar, int soc_percent);

lv_obj_t *pp_panel_create(lv_obj_t *parent, int x, int y, int width, int height);
lv_obj_t *pp_label_create(lv_obj_t *parent, const char *text,
                          const lv_font_t *font, uint32_t color, int x, int y,
                          int width);
lv_obj_t *pp_icon_create(lv_obj_t *parent, pp_icon_id_t icon, uint32_t color,
                         int x, int y);
void pp_icon_recolor(lv_obj_t *icon, uint32_t color);

void pp_page_title(lv_obj_t *screen, const char *title);
void pp_footer(lv_obj_t *screen, const char *hint);
void pp_divider(lv_obj_t *screen, int y);

// Selection feedback shared by every list row.
void pp_row_style(lv_obj_t *panel, bool selected);
void pp_row_text_color(lv_obj_t *label, bool selected);

// Font-aware text setter for dynamic content.  Unsupported code points are
// replaced with '?' and logged as U+XXXX so a missing glyph is visible instead
// of a silent blank.  pp_label_set_text_safe uses the raw string; callers that
// need a display limit first use pp_text_copy_display().
void pp_label_set_text_safe(lv_obj_t *label, const char *text);

lv_obj_t *pp_progress_create(lv_obj_t *parent, int x, int y, int width,
                             int height, uint32_t color);
void pp_progress_set(lv_obj_t *fill, int percent);

// A modal-style full-screen page with title/body/hint; used for notices such
// as a missing QR image.  OK or long OK dismisses it (handled by the caller).
lv_obj_t *pp_notice_create(const char *title, const char *body,
                           const char *hint);
