#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "data/games.h"
#include "data/nfc.h"
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
    snprintf(profile->weibo_handle, sizeof(profile->weibo_handle), "@playerpass");
    snprintf(profile->weibo_url, sizeof(profile->weibo_url),
             "https://weibo.com/u/1234567890");
    snprintf(profile->wechat_name, sizeof(profile->wechat_name), "小陈");
    snprintf(profile->wechat_id, sizeof(profile->wechat_id), "player_pass_wx");
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
    CHECK_STR_EQ(parsed.weibo_handle, profile.weibo_handle);
    CHECK_STR_EQ(parsed.weibo_url, profile.weibo_url);
    CHECK_STR_EQ(parsed.wechat_name, profile.wechat_name);
    CHECK_STR_EQ(parsed.wechat_id, profile.wechat_id);
    cJSON_free(json);

    pp_profile_images_t images;
    pp_profile_images_clear(&images);
    images.avatar = true;
    images.steam_qr = true;
    images.wechat_pay_qr = true;
    json = pp_profile_to_json(&profile, &images);
    CHECK(json != NULL);
    if (json) {
        root = cJSON_Parse(json);
        cJSON *flags = cJSON_GetObjectItemCaseSensitive(root, "images");
        CHECK(cJSON_IsObject(flags));
        CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(flags, "avatar")));
        CHECK(cJSON_IsFalse(
            cJSON_GetObjectItemCaseSensitive(flags, "switch_qr")));
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
    profile.weibo_url[0] = '\0';
    CHECK(pp_profile_validate(&profile, error, sizeof(error)));

    profile_fixture(&profile);
    snprintf(profile.weibo_url, sizeof(profile.weibo_url), "ftp://example.com");
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));
    snprintf(profile.weibo_url, sizeof(profile.weibo_url),
             "https://example.com/a b");
    CHECK(!pp_profile_validate(&profile, error, sizeof(error)));

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
    CHECK(!pp_profile_from_json("{\"version\":2}", &profile, error,
                                sizeof(error)));
    CHECK(!pp_profile_from_json("{\"version\":1,\"name\":123}", &profile, error,
                                sizeof(error)));
    CHECK(pp_profile_from_json("{\"version\":1}", &profile, error,
                               sizeof(error)));
    CHECK(!pp_profile_from_json(
        "{\"version\":1,\"contact\":{\"weibo\":{\"url\":\"javascript:x\"}}}",
        &profile, error, sizeof(error)));
}

static void game_fixture(pp_game_t *game, const char *id, const char *title,
                         pp_game_group_t group)
{
    memset(game, 0, sizeof(*game));
    snprintf(game->id, sizeof(game->id), "%s", id);
    snprintf(game->title, sizeof(game->title), "%s", title);
    snprintf(game->note, sizeof(game->note), "%s 的简短介绍", title);
    snprintf(game->link, sizeof(game->link), "https://www.xiaoheihe.cn/app/%s",
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

static void test_nfc(void)
{
    pp_nfc_t nfc;
    pp_nfc_defaults(&nfc);
    CHECK(!nfc.enabled);
    char error[128];
    CHECK(pp_nfc_validate(&nfc, error, sizeof(error)));

    snprintf(nfc.label, sizeof(nfc.label), "小黑盒");
    nfc.enabled = true;
    nfc.type = PP_NFC_TYPE_URL;
    snprintf(nfc.content, sizeof(nfc.content),
             "https://www.xiaoheihe.cn/app/user/profile");
    CHECK(pp_nfc_validate(&nfc, error, sizeof(error)));

    char *json = pp_nfc_to_json(&nfc);
    CHECK(json != NULL);
    pp_nfc_t parsed;
    CHECK(pp_nfc_from_json(json, &parsed, error, sizeof(error)));
    CHECK(parsed.enabled);
    CHECK(parsed.type == PP_NFC_TYPE_URL);
    CHECK_STR_EQ(parsed.content, nfc.content);
    cJSON_free(json);

    nfc.type = PP_NFC_TYPE_URL;
    snprintf(nfc.content, sizeof(nfc.content), "not a url");
    CHECK(!pp_nfc_validate(&nfc, error, sizeof(error)));

    nfc.type = PP_NFC_TYPE_TEXT;
    snprintf(nfc.content, sizeof(nfc.content), "第一行\n第二行");
    CHECK(pp_nfc_validate(&nfc, error, sizeof(error)));

    nfc.enabled = true;
    nfc.content[0] = '\0';
    CHECK(!pp_nfc_validate(&nfc, error, sizeof(error)));

    CHECK(pp_nfc_from_json("{\"version\":1,\"type\":\"url\"}", &parsed, error,
                           sizeof(error)));
    CHECK(pp_nfc_from_json("{\"version\":1,\"type\":\"text\",\"enabled\":false,"
                           "\"content\":\"\"}",
                           &parsed, error, sizeof(error)));
    CHECK(!pp_nfc_from_json("{\"version\":2,\"type\":\"url\"}", &parsed, error,
                            sizeof(error)));
}

int main(void)
{
    test_profile_roundtrip();
    test_profile_validation();
    test_profile_json_errors();
    test_games_order_and_roundtrip();
    test_games_validation();
    test_nfc();
    return pp_test_result("test_models");
}
