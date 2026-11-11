#include "image/image_view.h"

#include <stdlib.h>
#include <string.h>

pp_storage_err_t pp_image_load(const pp_storage_t *storage, const char *rel,
                               pp_loaded_image_t *image)
{
    if (!storage || !rel || !image) return PP_STORAGE_ERR_INVALID;
    memset(image, 0, sizeof(*image));

    size_t size = 0;
    pp_storage_err_t err = pp_storage_size(storage, rel, &size);
    if (err != PP_STORAGE_OK) return err;
    if (size > PP_IMAGE_HEADER_BYTES + PP_IMAGE_MAX_BYTES) {
        return PP_STORAGE_ERR_TOO_BIG;
    }

    uint8_t *file = malloc(size + 1);
    if (!file) return PP_STORAGE_ERR_NO_MEMORY;
    err = pp_storage_read(storage, rel, file, size + 1, &size);
    if (err != PP_STORAGE_OK) {
        free(file);
        return err;
    }

    pp_image_meta_t meta;
    const pp_image_err_t valid =
        pp_image_validate(file, size, &meta);
    if (valid != PP_IMAGE_OK) {
        free(file);
        return PP_STORAGE_ERR_INVALID;
    }

    uint8_t *pixels = file;
    image->data = pixels;
    image->width = meta.width;
    image->height = meta.height;
    image->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    image->dsc.header.w = meta.width;
    image->dsc.header.h = meta.height;
    image->dsc.header.stride = meta.width * 2u;
    image->dsc.data_size = meta.data_len;
    // Keep the validated header in the same allocation and point LVGL at the
    // pixel area; this avoids a second full-size copy during page changes.
    image->dsc.data = pixels + PP_IMAGE_HEADER_BYTES;
    return PP_STORAGE_OK;
}

void pp_image_free(pp_loaded_image_t *image)
{
    if (!image) return;
    free(image->data);
    memset(image, 0, sizeof(*image));
}

lv_obj_t *pp_image_widget_create(lv_obj_t *parent, const pp_loaded_image_t *image,
                                 int x, int y, int width, int height)
{
    if (!parent || !image || !image->data) return NULL;
    lv_obj_t *widget = lv_image_create(parent);
    if (!widget) return NULL;
    lv_image_set_src(widget, &image->dsc);
    lv_obj_set_pos(widget, x, y);
    lv_obj_set_size(widget, width, height);
    lv_image_set_inner_align(widget, LV_IMAGE_ALIGN_STRETCH);
    lv_image_set_antialias(widget, false);
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_SCROLLABLE);
    return widget;
}
