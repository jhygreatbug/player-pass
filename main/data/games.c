#include "data/games.h"

#include <stdlib.h>
#include <string.h>

#include "app/text_utf8.h"
#include "cJSON.h"
#include "data/data_validate.h"

void pp_games_init(pp_games_t *games)
{
    if (!games) return;
    memset(games, 0, sizeof(*games));
}

void pp_games_free(pp_games_t *games)
{
    if (!games) return;
    free(games->items);
    memset(games, 0, sizeof(*games));
}

bool pp_game_id_valid(const char *id)
{
    if (!id) return false;
    const size_t length = strlen(id);
    if (length < 4 || length > PP_GAME_ID_MAX) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

const char *pp_game_group_to_string(pp_game_group_t group)
{
    return group == PP_GAME_GROUP_PLAYING ? "playing" : "favorite";
}

bool pp_game_group_from_string(const char *text, pp_game_group_t *group)
{
    if (!text || !group) return false;
    if (strcmp(text, "favorite") == 0) {
        *group = PP_GAME_GROUP_FAVORITE;
        return true;
    }
    if (strcmp(text, "playing") == 0) {
        *group = PP_GAME_GROUP_PLAYING;
        return true;
    }
    return false;
}

bool pp_games_add(pp_games_t *games, const pp_game_t *game)
{
    if (!games || !game) return false;
    if (games->count >= PP_GAMES_SAFETY_LIMIT) return false;
    if (!pp_game_id_valid(game->id)) return false;
    if (pp_games_find(games, game->id)) return false;
    if (games->count == games->capacity) {
        size_t capacity = games->capacity ? games->capacity * 2 : 8;
        if (capacity > PP_GAMES_SAFETY_LIMIT) capacity = PP_GAMES_SAFETY_LIMIT;
        pp_game_t *items = realloc(games->items, capacity * sizeof(*items));
        if (!items) return false;
        games->items = items;
        games->capacity = capacity;
    }
    games->items[games->count++] = *game;
    return true;
}

pp_game_t *pp_games_find(pp_games_t *games, const char *id)
{
    if (!games || !id) return NULL;
    for (size_t i = 0; i < games->count; ++i) {
        if (strcmp(games->items[i].id, id) == 0) return &games->items[i];
    }
    return NULL;
}

const pp_game_t *pp_games_find_const(const pp_games_t *games, const char *id)
{
    if (!games || !id) return NULL;
    for (size_t i = 0; i < games->count; ++i) {
        if (strcmp(games->items[i].id, id) == 0) return &games->items[i];
    }
    return NULL;
}

bool pp_games_remove(pp_games_t *games, const char *id)
{
    if (!games || !id) return false;
    for (size_t i = 0; i < games->count; ++i) {
        if (strcmp(games->items[i].id, id) != 0) continue;
        memmove(&games->items[i], &games->items[i + 1],
                (games->count - i - 1) * sizeof(games->items[0]));
        --games->count;
        return true;
    }
    return false;
}

void pp_games_normalize(pp_games_t *games)
{
    if (!games || games->count < 2) return;
    size_t write = 0;
    for (size_t i = 0; i < games->count; ++i) {
        if (games->items[i].group != PP_GAME_GROUP_FAVORITE) continue;
        if (i != write) {
            const pp_game_t moving = games->items[i];
            memmove(&games->items[write + 1], &games->items[write],
                    (i - write) * sizeof(games->items[0]));
            games->items[write] = moving;
        }
        ++write;
    }
}

bool pp_games_validate(const pp_games_t *games, char *error, size_t error_size)
{
    if (!games) {
        pp_set_error(error, error_size, "games is null");
        return false;
    }
    if (games->count > PP_GAMES_SAFETY_LIMIT) {
        pp_set_error(error, error_size, "too many games (limit %u)",
                     (unsigned)PP_GAMES_SAFETY_LIMIT);
        return false;
    }
    for (size_t i = 0; i < games->count; ++i) {
        const pp_game_t *game = &games->items[i];
        if (!pp_game_id_valid(game->id)) {
            pp_set_error(error, error_size, "games[%u].id is invalid",
                         (unsigned)i);
            return false;
        }
        for (size_t j = i + 1; j < games->count; ++j) {
            if (strcmp(game->id, games->items[j].id) == 0) {
                pp_set_error(error, error_size, "duplicate game id: %s",
                             game->id);
                return false;
            }
        }
        if (!pp_field_valid(game->title, PP_GAME_TITLE_CP, false) ||
            game->title[0] == '\0') {
            pp_set_error(error, error_size, "games[%u].title is invalid",
                         (unsigned)i);
            return false;
        }
        if (!pp_field_valid(game->note, PP_GAME_NOTE_CP, true)) {
            pp_set_error(error, error_size, "games[%u].note is invalid",
                         (unsigned)i);
            return false;
        }
        if (!pp_optional_url_valid(game->link)) {
            pp_set_error(error, error_size, "games[%u].link is not a valid URL",
                         (unsigned)i);
            return false;
        }
        if (game->group != PP_GAME_GROUP_FAVORITE &&
            game->group != PP_GAME_GROUP_PLAYING) {
            pp_set_error(error, error_size, "games[%u].group is invalid",
                         (unsigned)i);
            return false;
        }
    }
    return true;
}

static bool get_string(const cJSON *parent, const char *key, char *dst,
                       size_t dst_size, size_t max_codepoints, bool required,
                       const char *field, char *error, size_t error_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    if (!item || cJSON_IsNull(item)) {
        if (required) {
            pp_set_error(error, error_size, "missing field: %s", field);
            return false;
        }
        dst[0] = '\0';
        return true;
    }
    if (!cJSON_IsString(item)) {
        pp_set_error(error, error_size, "%s must be a string", field);
        return false;
    }
    const pp_text_result_t result =
        pp_text_copy(dst, dst_size, item->valuestring, max_codepoints);
    if (result == PP_TEXT_INVALID_UTF8) {
        pp_set_error(error, error_size, "%s is not valid UTF-8", field);
        return false;
    }
    if (result == PP_TEXT_TOO_LONG) {
        pp_set_error(error, error_size, "%s exceeds %u characters", field,
                     (unsigned)max_codepoints);
        return false;
    }
    if (result != PP_TEXT_OK) {
        pp_set_error(error, error_size, "%s does not fit its buffer", field);
        return false;
    }
    return true;
}

char *pp_games_to_json(const pp_games_t *games, bool include_covers)
{
    if (!games) return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "version", PP_GAMES_VERSION);
    cJSON *array = cJSON_AddArrayToObject(root, "games");
    if (!array) {
        cJSON_Delete(root);
        return NULL;
    }
    for (size_t i = 0; i < games->count; ++i) {
        const pp_game_t *game = &games->items[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(item, "id", game->id);
        cJSON_AddStringToObject(item, "title", game->title);
        cJSON_AddStringToObject(item, "group",
                                pp_game_group_to_string(game->group));
        cJSON_AddStringToObject(item, "note", game->note);
        cJSON_AddStringToObject(item, "link", game->link);
        if (include_covers) {
            cJSON_AddBoolToObject(item, "cover", game->has_cover);
        }
        cJSON_AddItemToArray(array, item);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

bool pp_games_from_json(const char *json, pp_games_t *games, char *error,
                        size_t error_size)
{
    if (!json || !games) {
        pp_set_error(error, error_size, "empty game list");
        return false;
    }
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        pp_set_error(error, error_size, "games file is not valid JSON");
        return false;
    }
    pp_games_t parsed;
    pp_games_init(&parsed);
    bool ok = false;

    if (!cJSON_IsObject(root)) {
        pp_set_error(error, error_size, "games root must be an object");
        goto done;
    }
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valuedouble != PP_GAMES_VERSION) {
        pp_set_error(error, error_size, "unsupported games version");
        goto done;
    }
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "games");
    if (!cJSON_IsArray(array)) {
        pp_set_error(error, error_size, "games must be an array");
        goto done;
    }
    const int count = cJSON_GetArraySize(array);
    if (count < 0 || count > PP_GAMES_SAFETY_LIMIT) {
        pp_set_error(error, error_size, "too many games (limit %u)",
                     (unsigned)PP_GAMES_SAFETY_LIMIT);
        goto done;
    }

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        pp_game_t game;
        memset(&game, 0, sizeof(game));
        if (!cJSON_IsObject(item)) {
            pp_set_error(error, error_size, "each game must be an object");
            goto done;
        }
        if (!get_string(item, "id", game.id, sizeof(game.id), PP_GAME_ID_MAX,
                        true, "id", error, error_size) ||
            !get_string(item, "title", game.title, sizeof(game.title),
                        PP_GAME_TITLE_CP, true, "title", error, error_size) ||
            !get_string(item, "note", game.note, sizeof(game.note),
                        PP_GAME_NOTE_CP, false, "note", error, error_size) ||
            !get_string(item, "link", game.link, sizeof(game.link), 480, false,
                        "link", error, error_size)) {
            goto done;
        }
        const cJSON *group = cJSON_GetObjectItemCaseSensitive(item, "group");
        if (!cJSON_IsString(group) ||
            !pp_game_group_from_string(group->valuestring, &game.group)) {
            pp_set_error(error, error_size,
                         "group must be \"favorite\" or \"playing\"");
            goto done;
        }
        game.has_cover = false;
        if (!pp_games_add(&parsed, &game)) {
            pp_set_error(error, error_size,
                         "duplicate or invalid game entry");
            goto done;
        }
    }

    if (!pp_games_validate(&parsed, error, error_size)) goto done;
    pp_games_normalize(&parsed);
    pp_games_free(games);
    *games = parsed;
    ok = true;

done:
    if (!ok) pp_games_free(&parsed);
    cJSON_Delete(root);
    return ok;
}
