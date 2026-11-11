#include "ui/ui_theme.h"

#include <stdio.h>
#include <string.h>

#include "app/text_utf8.h"
#include "esp_log.h"
#include "font/pp_font.h"

static const char *TAG = "ui";

static void label_base(lv_obj_t *label, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_border_width(label, 0, 0);
}

lv_obj_t *pp_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    if (!screen) return NULL;
    lv_obj_set_style_bg_color(screen, lv_color_hex(PP_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(screen, &pp_font_12, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(PP_COLOR_TEXT), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

pp_topbar_t pp_topbar_create(lv_obj_t *screen)
{
    pp_topbar_t bar = { 0 };
    bar.root = lv_obj_create(screen);
    lv_obj_set_pos(bar.root, 0, 0);
    lv_obj_set_size(bar.root, PP_SCREEN_W, PP_TOPBAR_H);
    lv_obj_set_style_bg_opa(bar.root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar.root, 0, 0);
    lv_obj_set_style_pad_all(bar.root, 0, 0);
    lv_obj_remove_flag(bar.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *brand = pp_label_create(bar.root, "PLAYER PASS", &pp_font_12,
                                      PP_COLOR_MUTED, 12, 6, 120);
    lv_label_set_long_mode(brand, LV_LABEL_LONG_MODE_CLIP);

    bar.battery_text = pp_label_create(bar.root, "--", &pp_font_12,
                                       PP_COLOR_MUTED, 170, 6, 38);
    lv_obj_set_style_text_align(bar.battery_text, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *battery = lv_obj_create(bar.root);
    lv_obj_set_pos(battery, 214, 7);
    lv_obj_set_size(battery, 16, 10);
    lv_obj_set_style_bg_opa(battery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery, 1, 0);
    lv_obj_set_style_border_color(battery, lv_color_hex(PP_COLOR_MUTED), 0);
    lv_obj_set_style_radius(battery, 0, 0);
    lv_obj_set_style_pad_all(battery, 0, 0);
    lv_obj_remove_flag(battery, LV_OBJ_FLAG_SCROLLABLE);

    bar.battery_fill = lv_obj_create(battery);
    lv_obj_set_pos(bar.battery_fill, 1, 1);
    lv_obj_set_size(bar.battery_fill, 12, 6);
    lv_obj_set_style_bg_color(bar.battery_fill, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(bar.battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar.battery_fill, 0, 0);
    lv_obj_set_style_radius(bar.battery_fill, 0, 0);
    lv_obj_set_style_pad_all(bar.battery_fill, 0, 0);
    lv_obj_remove_flag(bar.battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar.battery_fill, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *nub = lv_obj_create(bar.root);
    lv_obj_set_pos(nub, 230, 9);
    lv_obj_set_size(nub, 2, 6);
    lv_obj_set_style_bg_color(nub, lv_color_hex(PP_COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nub, 0, 0);
    lv_obj_set_style_radius(nub, 0, 0);
    lv_obj_set_style_pad_all(nub, 0, 0);
    lv_obj_remove_flag(nub, LV_OBJ_FLAG_SCROLLABLE);

    pp_divider(screen, PP_TOPBAR_H);
    return bar;
}

void pp_topbar_update(pp_topbar_t *bar, int soc_percent)
{
    if (!bar || !bar->battery_fill || !bar->battery_text) return;
    if (soc_percent < 0) {
        lv_label_set_text(bar->battery_text, "--");
        lv_obj_set_style_text_color(bar->battery_text,
                                    lv_color_hex(PP_COLOR_DIM), 0);
        lv_obj_add_flag(bar->battery_fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (soc_percent > 100) soc_percent = 100;
    char text[8];
    snprintf(text, sizeof(text), "%d%%", soc_percent);
    lv_label_set_text(bar->battery_text, text);
    uint32_t color = PP_COLOR_MINT;
    if (soc_percent <= 10) color = PP_COLOR_ERROR;
    else if (soc_percent <= 25) color = PP_COLOR_WARN;
    lv_obj_set_style_text_color(bar->battery_text, lv_color_hex(color), 0);
    lv_obj_clear_flag(bar->battery_fill, LV_OBJ_FLAG_HIDDEN);
    int width = (soc_percent * 12 + 99) / 100;
    if (width < 1 && soc_percent > 0) width = 1;
    lv_obj_set_size(bar->battery_fill, width, 6);
    lv_obj_set_style_bg_color(bar->battery_fill, lv_color_hex(color), 0);
}

lv_obj_t *pp_panel_create(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel) return NULL;
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PP_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(PP_COLOR_BORDER), 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

lv_obj_t *pp_label_create(lv_obj_t *parent, const char *text,
                          const lv_font_t *font, uint32_t color, int x, int y,
                          int width)
{
    lv_obj_t *label = lv_label_create(parent);
    if (!label) return NULL;
    label_base(label, font, color);
    lv_obj_set_pos(label, x, y);
    if (width > 0) lv_obj_set_width(label, width);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(label, text ? text : "");
    return label;
}

lv_obj_t *pp_icon_create(lv_obj_t *parent, pp_icon_id_t icon, uint32_t color,
                         int x, int y)
{
    const lv_image_dsc_t *dsc = pp_icon_get(icon);
    if (!dsc) return NULL;
    lv_obj_t *image = lv_image_create(parent);
    if (!image) return NULL;
    lv_image_set_src(image, dsc);
    lv_obj_set_pos(image, x, y);
    lv_obj_set_size(image, dsc->header.w, dsc->header.h);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(image, false);
    lv_obj_set_style_image_recolor(image, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    lv_obj_remove_flag(image, LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

void pp_icon_recolor(lv_obj_t *icon, uint32_t color)
{
    if (!icon) return;
    lv_obj_set_style_image_recolor(icon, lv_color_hex(color), 0);
}

void pp_page_title(lv_obj_t *screen, const char *title)
{
    if (!screen) return;
    lv_obj_t *accent = lv_obj_create(screen);
    lv_obj_set_pos(accent, 12, 33);
    lv_obj_set_size(accent, 3, 18);
    lv_obj_set_style_bg_color(accent, lv_color_hex(PP_COLOR_MINT), 0);
    lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(accent, 0, 0);
    lv_obj_set_style_radius(accent, 0, 0);
    lv_obj_set_style_pad_all(accent, 0, 0);
    lv_obj_remove_flag(accent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = pp_label_create(screen, title, &pp_font_24,
                                      PP_COLOR_TEXT, 21, 28, 207);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

void pp_divider(lv_obj_t *screen, int y)
{
    if (!screen) return;
    lv_obj_t *line = lv_obj_create(screen);
    lv_obj_set_pos(line, 12, y);
    lv_obj_set_size(line, 216, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(PP_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

void pp_footer(lv_obj_t *screen, const char *hint)
{
    if (!screen) return;
    pp_divider(screen, PP_FOOTER_Y - 2);
    lv_obj_t *label = pp_label_create(screen, hint, &pp_font_12,
                                      PP_COLOR_DIM, 12, PP_FOOTER_Y + 2, 216);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

void pp_row_style(lv_obj_t *panel, bool selected)
{
    if (!panel) return;
    lv_obj_set_style_border_width(panel, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(
        panel, lv_color_hex(selected ? PP_COLOR_MINT : PP_COLOR_BORDER), 0);
    lv_obj_set_style_bg_color(
        panel, lv_color_hex(selected ? PP_COLOR_PANEL_ALT : PP_COLOR_PANEL), 0);
}

void pp_row_text_color(lv_obj_t *label, bool selected)
{
    if (!label) return;
    lv_obj_set_style_text_color(
        label, lv_color_hex(selected ? PP_COLOR_MINT : PP_COLOR_TEXT), 0);
}

void pp_label_set_text_safe(lv_obj_t *label, const char *text)
{
    if (!label) return;
    char cleaned[512];
    pp_font_sanitize(text ? text : "", cleaned, sizeof(cleaned));
    lv_label_set_text(label, cleaned);

    uint32_t missing = 0;
    if (pp_font_first_missing(text ? text : "", &missing)) {
        ESP_LOGW(TAG, "字体缺字 U+%04lX，已显示为 '?'（需要重新生成字体）",
                 (unsigned long)missing);
    }
}

lv_obj_t *pp_progress_create(lv_obj_t *parent, int x, int y, int width,
                             int height, uint32_t color)
{
    lv_obj_t *track = lv_obj_create(parent);
    lv_obj_set_pos(track, x, y);
    lv_obj_set_size(track, width, height);
    lv_obj_set_style_bg_color(track, lv_color_hex(PP_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(PP_COLOR_BORDER), 0);
    lv_obj_set_style_radius(track, 0, 0);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *fill = lv_obj_create(track);
    lv_obj_set_pos(fill, 1, 1);
    lv_obj_set_size(fill, 0, height - 2);
    lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_set_style_radius(fill, 0, 0);
    lv_obj_set_style_pad_all(fill, 0, 0);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    return fill;
}

void pp_progress_set(lv_obj_t *fill, int percent)
{
    if (!fill) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_obj_t *track = lv_obj_get_parent(fill);
    if (!track) return;
    const int track_width = lv_obj_get_width(track);
    const int width = (track_width - 2) * percent / 100;
    lv_obj_set_size(fill, width, lv_obj_get_height(track) - 2);
}

lv_obj_t *pp_notice_create(const char *title, const char *body,
                           const char *hint)
{
    lv_obj_t *screen = pp_screen_create();
    if (!screen) return NULL;
    pp_topbar_create(screen);
    pp_page_title(screen, title);
    lv_obj_t *panel = pp_panel_create(screen, 12, 66, 216, 200);
    lv_obj_t *warning = pp_icon_create(panel, PP_ICON_WARNING, PP_COLOR_WARN,
                                       18, 18);
    (void)warning;
    lv_obj_t *message = pp_label_create(panel, body, &pp_font_12,
                                        PP_COLOR_TEXT, 18, 44, 180);
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    pp_footer(screen, hint);
    return screen;
}
