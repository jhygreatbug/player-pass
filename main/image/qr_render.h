// Render URL or text payloads into a QR image entirely on the device.
//
// Uses the vendored Nayuki encoder (MIT).  The result is an A8 mask sized for
// the 200x200 QR slot; the UI draws it black over a white panel, which keeps
// printed-code contrast even though the rest of the interface is dark.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "image/image_meta.h"
#include "lvgl.h"

typedef struct {
    uint8_t *mask;
    lv_image_dsc_t dsc;
} pp_qr_image_t;

// Encodes text (UTF-8, byte mode) with medium error correction and renders a
// quiet zone.  Returns false when the payload is too long for the supported
// version range or memory is exhausted.
bool pp_qr_render_text(const char *text, pp_qr_image_t *image);

// Same as above with an explicit square output size (for the management-screen
// QR).  The module scale is an integer, so small sizes can reject long payloads.
bool pp_qr_render_text_sized(const char *text, uint16_t size,
                             pp_qr_image_t *image);
void pp_qr_free(pp_qr_image_t *image);
