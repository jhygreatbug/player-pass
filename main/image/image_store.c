#include "image/image_store.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    pp_image_slot_t slot;
    const char *rel;
} SLOTS[] = {
    { "avatar", PP_IMAGE_SLOT_AVATAR, "img/avatar.bin" },
    { "qr_steam", PP_IMAGE_SLOT_QR_STEAM, "img/qr_steam.bin" },
    { "qr_switch", PP_IMAGE_SLOT_QR_SWITCH, "img/qr_switch.bin" },
    { "qr_psn", PP_IMAGE_SLOT_QR_PSN, "img/qr_psn.bin" },
    { "qr_wechat", PP_IMAGE_SLOT_QR_WECHAT, "img/qr_wechat.bin" },
    { "qr_wechat_pay", PP_IMAGE_SLOT_QR_WECHAT_PAY, "img/qr_wechat_pay.bin" },
};

static bool profile_slot(pp_image_slot_t slot)
{
    return slot >= PP_IMAGE_SLOT_AVATAR && slot <= PP_IMAGE_SLOT_QR_WECHAT_PAY;
}

bool pp_image_slot_parse(const char *name, pp_image_slot_info_t *info)
{
    if (!name || !info) return false;
    memset(info, 0, sizeof(*info));
    for (size_t i = 0; i < sizeof(SLOTS) / sizeof(SLOTS[0]); ++i) {
        if (strcmp(name, SLOTS[i].name) != 0) continue;
        info->slot = SLOTS[i].slot;
        info->kind = info->slot == PP_IMAGE_SLOT_AVATAR ? PP_IMAGE_KIND_AVATAR
                                                         : PP_IMAGE_KIND_QR;
        return true;
    }
    if (strncmp(name, "games/", 6) == 0) {
        const char *id = name + 6;
        if (!pp_game_id_valid(id)) return false;
        info->slot = PP_IMAGE_SLOT_GAME_COVER;
        info->kind = PP_IMAGE_KIND_COVER;
        snprintf(info->game_id, sizeof(info->game_id), "%s", id);
        return true;
    }
    return false;
}

bool pp_image_slot_path(const pp_image_slot_info_t *info, char *rel,
                        size_t rel_size)
{
    if (!info || !rel || rel_size == 0) return false;
    if (info->slot == PP_IMAGE_SLOT_GAME_COVER) {
        if (!pp_game_id_valid(info->game_id)) return false;
        const int written = snprintf(rel, rel_size, "img/games/%s.bin",
                                     info->game_id);
        return written > 0 && (size_t)written < rel_size;
    }
    for (size_t i = 0; i < sizeof(SLOTS) / sizeof(SLOTS[0]); ++i) {
        if (SLOTS[i].slot != info->slot) continue;
        const int written = snprintf(rel, rel_size, "%s", SLOTS[i].rel);
        return written > 0 && (size_t)written < rel_size;
    }
    return false;
}

bool pp_image_avatar_path(char *rel, size_t rel_size)
{
    const pp_image_slot_info_t info = { .slot = PP_IMAGE_SLOT_AVATAR,
                                        .kind = PP_IMAGE_KIND_AVATAR };
    return pp_image_slot_path(&info, rel, rel_size);
}

bool pp_image_profile_qr_path(pp_image_slot_t slot, char *rel, size_t rel_size)
{
    if (!profile_slot(slot) || slot == PP_IMAGE_SLOT_AVATAR) return false;
    const pp_image_slot_info_t info = { .slot = slot, .kind = PP_IMAGE_KIND_QR };
    return pp_image_slot_path(&info, rel, rel_size);
}

bool pp_image_cover_path(const char *game_id, char *rel, size_t rel_size)
{
    if (!pp_game_id_valid(game_id)) return false;
    const int written = snprintf(rel, rel_size, "img/games/%s.bin", game_id);
    return written > 0 && (size_t)written < rel_size;
}

static pp_storage_err_t check(const pp_storage_t *storage, const char *rel,
                              bool *present)
{
    bool exists = false;
    const pp_storage_err_t err = pp_storage_exists(storage, rel, &exists);
    if (err != PP_STORAGE_OK) return err;
    // A present name is not enough: a zero-byte or foreign file is treated as
    // absent so the device never tries to render it.
    if (!exists) {
        *present = false;
        return PP_STORAGE_OK;
    }
    size_t size = 0;
    const pp_storage_err_t size_err = pp_storage_size(storage, rel, &size);
    *present = size_err == PP_STORAGE_OK && size >= PP_IMAGE_HEADER_BYTES;
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_image_profile_flags(const pp_storage_t *storage,
                                        pp_profile_images_t *images)
{
    if (!storage || !images) return PP_STORAGE_ERR_INVALID;
    pp_profile_images_clear(images);
    const pp_image_slot_t slots[] = {
        PP_IMAGE_SLOT_AVATAR,       PP_IMAGE_SLOT_QR_STEAM,
        PP_IMAGE_SLOT_QR_SWITCH,    PP_IMAGE_SLOT_QR_PSN,
        PP_IMAGE_SLOT_QR_WECHAT,    PP_IMAGE_SLOT_QR_WECHAT_PAY,
    };
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        char rel[PP_STORAGE_PATH_MAX];
        if (!pp_image_profile_qr_path(slots[i], rel, sizeof(rel))) {
            if (slots[i] == PP_IMAGE_SLOT_AVATAR) {
                if (!pp_image_avatar_path(rel, sizeof(rel))) {
                    return PP_STORAGE_ERR_INVALID;
                }
            } else {
                return PP_STORAGE_ERR_INVALID;
            }
        }
        bool present = false;
        const pp_storage_err_t err = check(storage, rel, &present);
        if (err != PP_STORAGE_OK) return err;
        switch (slots[i]) {
        case PP_IMAGE_SLOT_AVATAR: images->avatar = present; break;
        case PP_IMAGE_SLOT_QR_STEAM: images->steam_qr = present; break;
        case PP_IMAGE_SLOT_QR_SWITCH: images->switch_qr = present; break;
        case PP_IMAGE_SLOT_QR_PSN: images->psn_qr = present; break;
        case PP_IMAGE_SLOT_QR_WECHAT: images->wechat_qr = present; break;
        case PP_IMAGE_SLOT_QR_WECHAT_PAY: images->wechat_pay_qr = present; break;
        default: break;
        }
    }
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_image_game_flags(const pp_storage_t *storage,
                                     pp_games_t *games)
{
    if (!storage || !games) return PP_STORAGE_ERR_INVALID;
    for (size_t i = 0; i < games->count; ++i) {
        char rel[PP_STORAGE_PATH_MAX];
        if (!pp_image_cover_path(games->items[i].id, rel, sizeof(rel))) {
            games->items[i].has_cover = false;
            continue;
        }
        bool present = false;
        const pp_storage_err_t err = check(storage, rel, &present);
        if (err != PP_STORAGE_OK) return err;
        games->items[i].has_cover = present;
    }
    return PP_STORAGE_OK;
}
