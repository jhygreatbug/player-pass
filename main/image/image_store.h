// Image slot naming and presence checks.
//
// Slot names are the only image identifiers accepted over HTTP; they map to
// fixed files under /lfs/img.  Game covers live in /lfs/img/games/<game-id>.bin,
// so a game id is validated before it can become a path.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "data/games.h"
#include "data/profile.h"
#include "image/image_meta.h"
#include "storage/storage.h"

typedef enum {
    PP_IMAGE_SLOT_AVATAR = 0,
    PP_IMAGE_SLOT_QR_STEAM,
    PP_IMAGE_SLOT_QR_SWITCH,
    PP_IMAGE_SLOT_QR_PSN,
    PP_IMAGE_SLOT_QR_WECHAT,
    PP_IMAGE_SLOT_QR_WECHAT_PAY,
    PP_IMAGE_SLOT_GAME_COVER,
} pp_image_slot_t;

typedef struct {
    pp_image_slot_t slot;
    pp_image_kind_t kind;
    char game_id[PP_GAME_ID_MAX + 1];
} pp_image_slot_info_t;

// Accepts "avatar", "qr_steam", "qr_switch", "qr_psn", "qr_wechat",
// "qr_wechat_pay", and "games/<valid-id>".
bool pp_image_slot_parse(const char *name, pp_image_slot_info_t *info);

// Fills rel with the repository-relative LittleFS path.
bool pp_image_slot_path(const pp_image_slot_info_t *info, char *rel,
                        size_t rel_size);

// Convenience for callers that only have a profile/game id.
bool pp_image_avatar_path(char *rel, size_t rel_size);
bool pp_image_profile_qr_path(pp_image_slot_t slot, char *rel, size_t rel_size);
bool pp_image_cover_path(const char *game_id, char *rel, size_t rel_size);

pp_storage_err_t pp_image_profile_flags(const pp_storage_t *storage,
                                        pp_profile_images_t *images);
pp_storage_err_t pp_image_game_flags(const pp_storage_t *storage,
                                     pp_games_t *games);
