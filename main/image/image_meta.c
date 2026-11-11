#include "image/image_meta.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void write_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);
    p[1] = (uint8_t)((value >> 8) & 0xFFu);
    p[2] = (uint8_t)((value >> 16) & 0xFFu);
    p[3] = (uint8_t)((value >> 24) & 0xFFu);
}

const char *pp_image_error_name(pp_image_err_t error)
{
    switch (error) {
    case PP_IMAGE_OK: return "ok";
    case PP_IMAGE_ERR_SHORT: return "short";
    case PP_IMAGE_ERR_MAGIC: return "magic";
    case PP_IMAGE_ERR_VERSION: return "version";
    case PP_IMAGE_ERR_FORMAT: return "format";
    case PP_IMAGE_ERR_DIMENSIONS: return "dimensions";
    case PP_IMAGE_ERR_LENGTH: return "length";
    case PP_IMAGE_ERR_CRC: return "crc";
    case PP_IMAGE_ERR_KIND: return "kind";
    default: return "unknown";
    }
}

void pp_image_build_header(uint8_t *header, uint16_t width, uint16_t height,
                           uint32_t data_len, uint32_t crc32)
{
    if (!header) return;
    write_le32(header, PP_IMAGE_MAGIC);
    header[4] = (uint8_t)PP_IMAGE_VERSION;
    header[5] = (uint8_t)PP_IMAGE_FORMAT_RGB565_LE;
    header[6] = 0;
    header[7] = 0;
    write_le16(header + 8, width);
    write_le16(header + 10, height);
    write_le32(header + 12, data_len);
    write_le32(header + 16, crc32);
}

pp_image_err_t pp_image_parse_header(const void *buffer, size_t length,
                                     pp_image_meta_t *meta)
{
    if (!buffer || !meta || length < PP_IMAGE_HEADER_BYTES) {
        return PP_IMAGE_ERR_SHORT;
    }
    const uint8_t *p = (const uint8_t *)buffer;
    if (read_le32(p) != PP_IMAGE_MAGIC) return PP_IMAGE_ERR_MAGIC;
    if (p[4] != PP_IMAGE_VERSION) return PP_IMAGE_ERR_VERSION;
    if (p[5] != PP_IMAGE_FORMAT_RGB565_LE) return PP_IMAGE_ERR_FORMAT;

    const uint16_t width = read_le16(p + 8);
    const uint16_t height = read_le16(p + 10);
    const uint32_t data_len = read_le32(p + 12);
    if (width == 0 || height == 0 || width > PP_IMAGE_MAX_DIMENSION ||
        height > PP_IMAGE_MAX_DIMENSION) {
        return PP_IMAGE_ERR_DIMENSIONS;
    }
    if (data_len != (uint32_t)width * (uint32_t)height * 2u) {
        return PP_IMAGE_ERR_LENGTH;
    }
    meta->width = width;
    meta->height = height;
    meta->data_len = data_len;
    meta->crc32 = read_le32(p + 16);
    return PP_IMAGE_OK;
}

pp_image_err_t pp_image_validate(const void *buffer, size_t length,
                                 pp_image_meta_t *meta)
{
    pp_image_err_t err = pp_image_parse_header(buffer, length, meta);
    if (err != PP_IMAGE_OK) return err;
    if (length != PP_IMAGE_HEADER_BYTES + (size_t)meta->data_len) {
        return PP_IMAGE_ERR_LENGTH;
    }
    const uint8_t *data = (const uint8_t *)buffer + PP_IMAGE_HEADER_BYTES;
    const uint32_t crc = pp_crc32(data, meta->data_len);
    if (crc != meta->crc32) return PP_IMAGE_ERR_CRC;
    return PP_IMAGE_OK;
}

bool pp_image_kind_dimensions(pp_image_kind_t kind, uint16_t *width,
                              uint16_t *height)
{
    uint16_t w;
    uint16_t h;
    switch (kind) {
    case PP_IMAGE_KIND_AVATAR:
        w = PP_IMAGE_AVATAR_W;
        h = PP_IMAGE_AVATAR_H;
        break;
    case PP_IMAGE_KIND_COVER:
        w = PP_IMAGE_COVER_W;
        h = PP_IMAGE_COVER_H;
        break;
    case PP_IMAGE_KIND_QR:
        w = PP_IMAGE_QR_W;
        h = PP_IMAGE_QR_H;
        break;
    default:
        return false;
    }
    if (width) *width = w;
    if (height) *height = h;
    return true;
}

size_t pp_image_kind_file_bytes(pp_image_kind_t kind)
{
    uint16_t w = 0;
    uint16_t h = 0;
    if (!pp_image_kind_dimensions(kind, &w, &h)) return 0;
    return PP_IMAGE_HEADER_BYTES + (size_t)w * h * 2u;
}

uint32_t pp_crc32_update(uint32_t crc, const void *data, size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t pp_crc32(const void *data, size_t length)
{
    return pp_crc32_update(0, data, length);
}
