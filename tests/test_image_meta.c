#include <stdint.h>
#include <string.h>

#include "image/image_meta.h"
#include "pp_test.h"

static void test_crc_vector(void)
{
    CHECK(pp_crc32("123456789", 9) == 0xCBF43926u);
    CHECK(pp_crc32("", 0) == 0u);
    uint32_t running = pp_crc32_update(0, "1234", 4);
    running = pp_crc32_update(running, "56789", 5);
    CHECK(running == 0xCBF43926u);
}

static void test_header_and_validation(void)
{
    const uint16_t w = PP_IMAGE_COVER_W;
    const uint16_t h = PP_IMAGE_COVER_H;
    const uint32_t data_len = (uint32_t)w * h * 2u;
    uint8_t file[PP_IMAGE_HEADER_BYTES + PP_IMAGE_COVER_W * PP_IMAGE_COVER_H * 2];
    memset(file + PP_IMAGE_HEADER_BYTES, 0x5A, data_len);
    const uint32_t crc = pp_crc32(file + PP_IMAGE_HEADER_BYTES, data_len);
    pp_image_build_header(file, w, h, data_len, crc);

    pp_image_meta_t meta;
    CHECK(pp_image_validate(file, sizeof(file), &meta) == PP_IMAGE_OK);
    CHECK(meta.width == w);
    CHECK(meta.height == h);
    CHECK(meta.data_len == data_len);
    CHECK(meta.crc32 == crc);

    pp_image_err_t err = pp_image_parse_header(file, sizeof(file), &meta);
    CHECK(err == PP_IMAGE_OK);

    uint8_t copy[sizeof(file)];
    memcpy(copy, file, sizeof(copy));
    copy[PP_IMAGE_HEADER_BYTES + 10] ^= 0xFF;
    CHECK(pp_image_validate(copy, sizeof(copy), &meta) == PP_IMAGE_ERR_CRC);

    CHECK(pp_image_validate(file, sizeof(file) - 1, &meta) ==
          PP_IMAGE_ERR_LENGTH);
    CHECK(pp_image_parse_header(file, 5, &meta) == PP_IMAGE_ERR_SHORT);

    memcpy(copy, file, sizeof(copy));
    copy[0] = 'X';
    CHECK(pp_image_validate(copy, sizeof(copy), &meta) == PP_IMAGE_ERR_MAGIC);

    memcpy(copy, file, sizeof(copy));
    copy[4] = 2;
    CHECK(pp_image_validate(copy, sizeof(copy), &meta) == PP_IMAGE_ERR_VERSION);

    memcpy(copy, file, sizeof(copy));
    copy[5] = 9;
    CHECK(pp_image_validate(copy, sizeof(copy), &meta) == PP_IMAGE_ERR_FORMAT);

    memcpy(copy, file, sizeof(copy));
    copy[8] = 0;
    copy[9] = 0;
    CHECK(pp_image_validate(copy, sizeof(copy), &meta) ==
          PP_IMAGE_ERR_DIMENSIONS);

    memcpy(copy, file, sizeof(copy));
    copy[12] ^= 1;
    CHECK(pp_image_validate(copy, sizeof(copy), &meta) == PP_IMAGE_ERR_LENGTH);
}

static void test_kinds(void)
{
    uint16_t w = 0;
    uint16_t h = 0;
    CHECK(pp_image_kind_dimensions(PP_IMAGE_KIND_AVATAR, &w, &h));
    CHECK(w == 88 && h == 88);
    CHECK(pp_image_kind_dimensions(PP_IMAGE_KIND_COVER, &w, &h));
    CHECK(w == 72 && h == 96);
    CHECK(pp_image_kind_dimensions(PP_IMAGE_KIND_QR, &w, &h));
    CHECK(w == 180 && h == 180);
    CHECK(pp_image_kind_file_bytes(PP_IMAGE_KIND_AVATAR) ==
          PP_IMAGE_HEADER_BYTES + 88u * 88u * 2u);
    CHECK(!pp_image_kind_dimensions((pp_image_kind_t)99, &w, &h));
}

int main(void)
{
    test_crc_vector();
    test_header_and_validation();
    test_kinds();
    return pp_test_result("test_image_meta");
}
