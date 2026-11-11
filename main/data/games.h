// Game list model.  The on-disk array is normalized to favorites first, then
// "playing", preserving the user's order inside each group.  Covers are files
// named by game id, so deleting a game can remove its image deterministically.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PP_GAMES_VERSION 1
#define PP_GAME_ID_MAX 24
#define PP_GAME_TITLE_CP 40
#define PP_GAME_NOTE_CP 120
#define PP_GAME_LINK_BYTES 480
#define PP_GAMES_SAFETY_LIMIT 200

typedef enum {
    PP_GAME_GROUP_FAVORITE = 0,
    PP_GAME_GROUP_PLAYING = 1,
} pp_game_group_t;

typedef struct {
    char id[PP_GAME_ID_MAX + 1];
    char title[PP_GAME_TITLE_CP * 4 + 1];
    char note[PP_GAME_NOTE_CP * 4 + 1];
    char link[PP_GAME_LINK_BYTES];
    pp_game_group_t group;
    bool has_cover;
} pp_game_t;

typedef struct {
    pp_game_t *items;
    size_t count;
    size_t capacity;
} pp_games_t;

void pp_games_init(pp_games_t *games);
void pp_games_free(pp_games_t *games);

// Copies the record.  Returns false on allocation failure or duplicate id.
bool pp_games_add(pp_games_t *games, const pp_game_t *game);
pp_game_t *pp_games_find(pp_games_t *games, const char *id);
const pp_game_t *pp_games_find_const(const pp_games_t *games, const char *id);
bool pp_games_remove(pp_games_t *games, const char *id);

// Stable partition: favorites first, playing second.
void pp_games_normalize(pp_games_t *games);

bool pp_game_id_valid(const char *id);
const char *pp_game_group_to_string(pp_game_group_t group);
bool pp_game_group_from_string(const char *text, pp_game_group_t *group);
bool pp_games_validate(const pp_games_t *games, char *error, size_t error_size);

// include_covers adds a boolean "cover" to each API record.  On-disk files use
// false because image presence is derived from the filesystem.
char *pp_games_to_json(const pp_games_t *games, bool include_covers);
bool pp_games_from_json(const char *json, pp_games_t *games, char *error,
                        size_t error_size);
