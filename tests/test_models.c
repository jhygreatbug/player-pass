#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "data/games.h"
#include "data/profile.h"
#include "pp_test.h"

static void profile_fixture(pp_profile_t *profile)
{
    pp_profile_defaults(profile);
    snprintf(profile->name, sizeof(profile->name), "阿陈");
    snprintf(profile->tagline, sizeof(profile->tagline),
             "每天玩一点游戏\n也做一点像素小玩意");
    snprintf(profile->steam_id, sizeof(profile->steam_id), "76561198000000000");
    snprintf(profile->switch_id, sizeof(profile->switch_id), "SW-1234-5678-9012");
    snprintf(profile->psn_id, sizeof(profile->psn_id), "player_pass");
    profile->contact_count = 2;
    snprintf(profile->contacts[0].platform,
             sizeof(profile->contacts[0].platform), "微博");
    snprintf(profile->contacts[0].account,
             sizeof(profile->contacts[0].account), "@playerpass");
    snprintf(profile->contacts[0].link, sizeof(profile->contacts[0].link),
             "https://weibo.com/u/1234567890");
    snprintf(profile->contacts[1].platform,
             sizeof(profile->contacts[1].platform), "微信");
    snprintf(profile->contacts[1].account,
             sizeof(profile->contacts[1].account), "player_pass_wx");
}

static void test_profile_roundtrip(void)
{
    pp_profile_t profile;
    profile_fixture(&profile);
    char error[128];
    CHECK(pp_profile_validate(&profile, error, sizeof(error)));

    char *json = pp_profile_to_json(&profile, NULL);
    CHECK(json != NULL);
    if (!json) return;
    cJSON *root = cJSON_Parse(json);
    CHECK(cJSON_IsObject(root));
    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    CHECK(cJSON_IsString(name));
    CHECK_STR_EQ(name->valuestring, "阿陈");
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "images") == NULL);
    cJSON_Delete(root);

    pp_profile_t parsed;
    CHECK(pp_profile_from_json(json, &parsed, error, sizeof(error)));
    CHECK_STR_EQ(parsed.name, profile.name);
    CHECK_STR_EQ(parsed.tagline, profile.tagline);
    CHECK_STR_EQ(parsed.steam_id, profile.steam_id);
    CHECK_STR_EQ(parsed.switch_id, profile.switch_id);
    CHECK_STR_EQ(parsed.psn_id, profile.psn_id);
    CHECK(parsed.contact_count == 2);
    CHECK_STR_EQ(parsed.contacts[0].platform, "微博");
    CHECK_STR_EQ(parsed.contacts[0].account, "@playerpass");
    CHECK_STR_EQ(parsed.contacts[0].link, "https://weibo.com/u/1234567890");
    CHECK_STR_EQ(parsed.contacts[1].platform, "微信");
    CHECK_STR_EQ(parsed.contacts[1].account, "player_pass_wx");
    CHECK_STR_EQ(parsed.contacts[1].link, "");
    cJSON_free(json);

    pp_profile_images_t images;
    pp_profile_images_clear(&images);
    images.avatar = true;
    json = pp_profile_to_json(&profile, &images);
    CHECK(json != NULL);
    if (json) {
        root = cJSON_Parse(json);
        cJSON *flags = cJSON_GetObjectItemCaseSensitive(root, "images");
        CHECK(cJSON_IsObject(flags));
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(flags, "avatar")));
        cJSON_Delete(root);
        cJSON_free(json);
    }
}

static void test_profile_validation(void)
{
    pp_profile_t profile;
    profile_fixture(&profile);
    char error[128];

    pp_profile_defaults(&profile);
    CHECK(pp_profile_validate(&profile, error, sizeof(error)));

    profile_fixture(&profile);
    profile.contacts[0].link[0] = '\0';
    CHECK(pp_profile_validate(&profile, error, sizeof(error)));

    profile_fixture(&profile);
    snprintf(profile.contacts[0].link, sizeof(profile.contacts[0].link),
             "ftp://example.com");
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));
    snprintf(profile.contacts[0].link, sizeof(profile.contacts[0].link),
             "https://example.com/a b");
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));

    profile_fixture(&profile);
    profile.contacts[0].platform[0] = '\0';
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));
    CHECK(strstr(error, "platform") != NULL);
    profile_fixture(&profile);
    profile.contact_count = PP_PROFILE_CONTACT_MAX + 1;
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));
    CHECK(strstr(error, "contacts") != NULL);

    profile_fixture(&profile);
    memset(profile.name, 'A', PP_PROFILE_NAME_CP + 1);
    profile.name[PP_PROFILE_NAME_CP + 1] = '\0';
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));
    CHECK(strstr(error, "name") != NULL);

    profile_fixture(&profile);
    profile.name[0] = '\x01';
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));

    profile_fixture(&profile);
    snprintf(profile.name, sizeof(profile.name), "%s",
             "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x9B\x9B\xE4\xBA\x94"
             "\xE5\x85\xAD\xE4\xB8\x83\xE5\x85\xAB\xE4\xB9\x9D\xE5\x8D\x81"
             "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x9B\x9B\xE4\xBA\x94"
             "\xE5\x85\xAD\xE4\xB8\x83\xE5\x85\xAB\xE4\xB9\x9D\xE5\x8D\x81"
             "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x9B\x9B\xE4\xBA\x94");
    CHECK(!pp_profile_validate(&profile, error, sizeof(error))); // 25 chars
}

static void test_profile_json_errors(void)
{
    pp_profile_t profile;
    char error[128];
    CHECK(!pp_profile_from_json("not json", &profile, error, sizeof(error)));
    CHECK(!pp_profile_from_json("[]", &profile, error, sizeof(error)));
    CHECK(!pp_profile_from_json("{\"version\":3}", &profile, error,
                                sizeof(error)));
    CHECK(!pp_profile_from_json("{\"version\":1.5}", &profile, error,
                                sizeof(error)));
    CHECK(!pp_profile_from_json("{\"version\":2,\"name\":123}", &profile, error,
                                sizeof(error)));
    CHECK(pp_profile_from_json("{\"version\":2}", &profile, error,
                               sizeof(error)));
    CHECK(!pp_profile_from_json("{\"version\":2,\"contacts\":{}}", &profile,
                                error, sizeof(error)));
    CHECK(!pp_profile_from_json(
        "{\"version\":2,\"contacts\":[{\"account\":\"x\"}]}", &profile, error,
        sizeof(error)));
    CHECK(!pp_profile_from_json(
        "{\"version\":2,\"contacts\":[{\"platform\":\"微博\","
        "\"link\":\"javascript:x\"}]}",
        &profile, error, sizeof(error)));
}

static void test_profile_legacy_migration(void)
{
    const char *json =
        "{\"version\":1,\"name\":\"旧名片\",\"contact\":{"
        "\"weibo\":{\"handle\":\"@old\",\"url\":\"https://weibo.com/u/1\"},"
        "\"wechat\":{\"name\":\"小陈\",\"id\":\"old_wx\"}}}";
    pp_profile_t profile;
    char error[128];
    CHECK(pp_profile_from_json(json, &profile, error, sizeof(error)));
    CHECK(profile.contact_count == 3);
    CHECK_STR_EQ(profile.contacts[0].platform, "微博");
    CHECK_STR_EQ(profile.contacts[0].account, "@old");
    CHECK_STR_EQ(profile.contacts[0].link, "https://weibo.com/u/1");
    CHECK_STR_EQ(profile.contacts[1].platform, "微信");
    CHECK_STR_EQ(profile.contacts[1].account, "小陈");
    CHECK_STR_EQ(profile.contacts[1].link, "");
    CHECK_STR_EQ(profile.contacts[2].platform, "微信号");
    CHECK_STR_EQ(profile.contacts[2].account, "old_wx");
}

static void game_fixture(pp_game_t *game, const char *id, const char *title,
                         pp_game_group_t group)
{
    memset(game, 0, sizeof(*game));
    snprintf(game->id, sizeof(game->id), "%s", id);
    snprintf(game->title, sizeof(game->title), "%s", title);
    snprintf(game->note, sizeof(game->note), "%s 的简短介绍", title);
    snprintf(game->link, sizeof(game->link), "https://example.com/game/%s",
             id);
    game->group = group;
}

static void test_games_order_and_roundtrip(void)
{
    pp_games_t games;
    pp_games_init(&games);
    pp_game_t a;
    pp_game_t b;
    pp_game_t c;
    game_fixture(&a, "g-aaaa", "游戏甲", PP_GAME_GROUP_FAVORITE);
    game_fixture(&b, "g-bbbb", "游戏乙", PP_GAME_GROUP_PLAYING);
    game_fixture(&c, "g-cccc", "游戏丙", PP_GAME_GROUP_FAVORITE);
    CHECK(pp_games_add(&games, &a));
    CHECK(pp_games_add(&games, &b));
    CHECK(pp_games_add(&games, &c));
    CHECK(!pp_games_add(&games, &a)); // duplicate id

    pp_games_normalize(&games);
    CHECK(games.count == 3);
    CHECK_STR_EQ(games.items[0].id, "g-aaaa");
    CHECK_STR_EQ(games.items[1].id, "g-cccc");
    CHECK_STR_EQ(games.items[2].id, "g-bbbb");

    games.items[0].has_cover = true;
    char error[128];
    CHECK(pp_games_validate(&games, error, sizeof(error)));
    char *json = pp_games_to_json(&games, true);
    CHECK(json != NULL);
    if (!json) {
        pp_games_free(&games);
        return;
    }
    cJSON *root = cJSON_Parse(json);
    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "games");
    CHECK(cJSON_GetArraySize(array) == 3);
    cJSON *first = cJSON_GetArrayItem(array, 0);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(first, "cover")));
    cJSON_Delete(root);

    pp_games_t parsed;
    pp_games_init(&parsed);
    CHECK(pp_games_from_json(json, &parsed, error, sizeof(error)));
    CHECK(parsed.count == 3);
    CHECK_STR_EQ(parsed.items[0].id, "g-aaaa");
    CHECK_STR_EQ(parsed.items[2].id, "g-bbbb");
    CHECK(parsed.items[0].has_cover == false);
    cJSON_free(json);

    CHECK(pp_games_remove(&parsed, "g-cccc"));
    CHECK(parsed.count == 2);
    CHECK(pp_games_find(&parsed, "g-aaaa") != NULL);
    CHECK(pp_games_find(&parsed, "g-cccc") == NULL);
    pp_games_free(&parsed);
    pp_games_free(&games);
}

static void test_games_validation(void)
{
    pp_games_t games;
    pp_games_init(&games);
    pp_game_t game;
    game_fixture(&game, "g-aaaa", "游戏甲", PP_GAME_GROUP_FAVORITE);
    char error[128];

    CHECK(game.title[0] != '\0');
    CHECK(pp_games_validate(&games, error, sizeof(error))); // empty list
    CHECK(pp_games_add(&games, &game));
    CHECK(pp_games_validate(&games, error, sizeof(error)));

    games.items[0].title[0] = '\0';
    CHECK(!pp_games_validate(&games, error, sizeof(error)));
    game_fixture(&games.items[0], "g-aaaa", "游戏甲", PP_GAME_GROUP_FAVORITE);

    snprintf(games.items[0].link, sizeof(games.items[0].link), "mailto:x@y");
    CHECK(!pp_games_validate(&games, error, sizeof(error)));
    snprintf(games.items[0].link, sizeof(games.items[0].link), "");
    CHECK(pp_games_validate(&games, error, sizeof(error)));

    pp_games_free(&games);

    CHECK(!pp_game_id_valid("g"));
    CHECK(!pp_game_id_valid("g_aaaa"));
    CHECK(pp_game_id_valid("g-aaaa1"));

    pp_games_init(&games);
    CHECK(!pp_games_from_json(
        "{\"version\":1,\"games\":[{\"id\":\"g-aaaa\",\"title\":\"A\","
        "\"group\":\"favorite\"},{\"id\":\"g-aaaa\",\"title\":\"B\","
        "\"group\":\"playing\"}]}",
        &games, error, sizeof(error)));
    CHECK(!pp_games_from_json(
        "{\"version\":1,\"games\":[{\"id\":\"g-aaaa\",\"title\":\"A\","
        "\"group\":\"other\"}]}",
        &games, error, sizeof(error)));
    pp_games_free(&games);
}

int main(void)
{
    test_profile_roundtrip();
    test_profile_validation();
    test_profile_json_errors();
    test_profile_legacy_migration();
    test_games_order_and_roundtrip();
    test_games_validation();
    return pp_test_result("test_models");
}
