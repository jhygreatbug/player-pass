#include "image/qr_render.h"

#include <stdlib.h>
#include <string.h>

#include "qrcodegen.h"

#define PP_QR_QUIET_MODULES 4
#define PP_QR_MAX_VERSION 15

static bool render_text(const char *text, uint16_t size, pp_qr_image_t *image)
{
    if (!text || !image || text[0] == '\0' || size == 0) return false;
    memset(image, 0, sizeof(*image));

    uint8_t *temp = malloc(qrcodegen_BUFFER_LEN_MAX);
    uint8_t *qr = malloc(qrcodegen_BUFFER_LEN_MAX);
    if (!temp || !qr) {
        free(temp);
        free(qr);
        return false;
    }
    const bool ok = qrcodegen_encodeText(
        text, temp, qr, qrcodegen_Ecc_MEDIUM, 1, PP_QR_MAX_VERSION,
        qrcodegen_Mask_AUTO, true);
    free(temp);
    if (!ok) {
        free(qr);
        return false;
    }

    const int modules = qrcodegen_getSize(qr);
    const int span_modules = modules + PP_QR_QUIET_MODULES * 2;
    const int scale = (int)size / span_modules;
    if (scale < 1) {
        free(qr);
        return false;
    }

    const uint32_t data_len = (uint32_t)size * size;
    uint8_t *mask = calloc(1, data_len);
    if (!mask) {
        free(qr);
        return false;
    }
    const int total = span_modules * scale;
    const int offset_x = ((int)size - total) / 2;
    const int offset_y = ((int)size - total) / 2;
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (!qrcodegen_getModule(qr, x, y)) continue;
            const int px = offset_x + (x + PP_QR_QUIET_MODULES) * scale;
            const int py = offset_y + (y + PP_QR_QUIET_MODULES) * scale;
            for (int dy = 0; dy < scale; ++dy) {
                uint8_t *row = mask + (size_t)(py + dy) * size + px;
                memset(row, 0xFF, (size_t)scale);
            }
        }
    }
    free(qr);

    image->mask = mask;
    image->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->dsc.header.cf = LV_COLOR_FORMAT_A8;
    image->dsc.header.w = size;
    image->dsc.header.h = size;
    image->dsc.header.stride = size;
    image->dsc.data_size = data_len;
    image->dsc.data = mask;
    return true;
}

bool pp_qr_render_text(const char *text, pp_qr_image_t *image)
{
    return render_text(text, PP_IMAGE_QR_W, image);
}

bool pp_qr_render_text_sized(const char *text, uint16_t size,
                             pp_qr_image_t *image)
{
    return render_text(text, size, image);
}

void pp_qr_free(pp_qr_image_t *image)
{
    if (!image) return;
    free(image->mask);
    memset(image, 0, sizeof(*image));
}
