// Load a validated PPIM file into RAM and wrap it in an LVGL image descriptor.
//
// The browser already produced the exact display resolution, so no decoder is
// involved.  A 96x128 cover costs 24 KB and a 200x200 QR costs 80 KB of internal
// RAM; callers load only the images their current screen shows and free them
// before leaving the screen.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "image/image_meta.h"
#include "lvgl.h"
#include "storage/storage.h"

typedef struct {
    lv_image_dsc_t dsc;
    uint8_t *data;
    uint16_t width;
    uint16_t height;
} pp_loaded_image_t;

pp_storage_err_t pp_image_load(const pp_storage_t *storage, const char *rel,
                               pp_loaded_image_t *image);
void pp_image_free(pp_loaded_image_t *image);

// Creates an image widget stretched into the requested box.  Antialiasing is
// disabled so QR codes stay crisp and pixel-art covers keep hard edges.
lv_obj_t *pp_image_widget_create(lv_obj_t *parent, const pp_loaded_image_t *image,
                                 int x, int y, int width, int height);
