// Image container used on LittleFS.
//
// The browser converts every upload to RGB565 little-endian pixels and wraps
// them in this 20-byte header, so the device never decodes JPEG/PNG and never
// needs a large decoder buffer.  The CRC lets the device reject a truncated or
// corrupted upload before it replaces the previous image.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PP_IMAGE_MAGIC 0x4D495050u // "PPIM" in file byte order
#define PP_IMAGE_VERSION 1u
#define PP_IMAGE_FORMAT_RGB565_LE 1u
#define PP_IMAGE_HEADER_BYTES 20u
#define PP_IMAGE_MAX_DIMENSION 320u
#define PP_IMAGE_MAX_BYTES (PP_IMAGE_MAX_DIMENSION * PP_IMAGE_MAX_DIMENSION * 2u)

#define PP_IMAGE_AVATAR_W 88u
#define PP_IMAGE_AVATAR_H 88u
// Steam store header art is 460 x 215; one column of cards spans the full
// 216 px content width, so the cover is stored at its display size.
#define PP_IMAGE_COVER_W 216u
#define PP_IMAGE_COVER_H 101u
#define PP_IMAGE_QR_W 180u
#define PP_IMAGE_QR_H 180u

typedef enum {
    PP_IMAGE_OK = 0,
    PP_IMAGE_ERR_SHORT,
    PP_IMAGE_ERR_MAGIC,
    PP_IMAGE_ERR_VERSION,
    PP_IMAGE_ERR_FORMAT,
    PP_IMAGE_ERR_DIMENSIONS,
    PP_IMAGE_ERR_LENGTH,
    PP_IMAGE_ERR_CRC,
    PP_IMAGE_ERR_KIND,
} pp_image_err_t;

typedef enum {
    PP_IMAGE_KIND_AVATAR = 0,
    PP_IMAGE_KIND_COVER,
} pp_image_kind_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t data_len;
    uint32_t crc32;
} pp_image_meta_t;

const char *pp_image_error_name(pp_image_err_t error);

// Build a header for tests and tools.  header must hold PP_IMAGE_HEADER_BYTES.
void pp_image_build_header(uint8_t *header, uint16_t width, uint16_t height,
                           uint32_t data_len, uint32_t crc32);

// Validate the header, exact pixel length, and CRC of a complete file buffer.
pp_image_err_t pp_image_validate(const void *buffer, size_t length,
                                 pp_image_meta_t *meta);

// Validate a header alone (used while a streamed upload is still in progress).
pp_image_err_t pp_image_parse_header(const void *buffer, size_t length,
                                     pp_image_meta_t *meta);

bool pp_image_kind_dimensions(pp_image_kind_t kind, uint16_t *width,
                              uint16_t *height);

// Expected complete-file size for a kind, including the header.
size_t pp_image_kind_file_bytes(pp_image_kind_t kind);

uint32_t pp_crc32(const void *data, size_t length);
uint32_t pp_crc32_update(uint32_t crc, const void *data, size_t length);
