#include <string.h>

#include "image/image_store.h"
#include "pp_test.h"

static void test_named_slots(void)
{
    static const struct {
        const char *name;
        pp_image_slot_t slot;
        pp_image_kind_t kind;
        const char *path;
    } cases[] = {
        { "avatar", PP_IMAGE_SLOT_AVATAR, PP_IMAGE_KIND_AVATAR, "img/avatar.bin" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        pp_image_slot_info_t info;
        CHECK(pp_image_slot_parse(cases[i].name, &info));
        CHECK(info.slot == cases[i].slot);
        CHECK(info.kind == cases[i].kind);
        char rel[PP_STORAGE_PATH_MAX];
        CHECK(pp_image_slot_path(&info, rel, sizeof(rel)));
        CHECK_STR_EQ(rel, cases[i].path);
    }
}

static void test_game_cover_slot(void)
{
    pp_image_slot_info_t info;
    CHECK(pp_image_slot_parse("games/g-a1b2c3", &info));
    CHECK(info.slot == PP_IMAGE_SLOT_GAME_COVER);
    CHECK(info.kind == PP_IMAGE_KIND_COVER);
    CHECK_STR_EQ(info.game_id, "g-a1b2c3");
    char rel[PP_STORAGE_PATH_MAX];
    CHECK(pp_image_slot_path(&info, rel, sizeof(rel)));
    CHECK_STR_EQ(rel, "img/games/g-a1b2c3.bin");
}

static void test_invalid_slots(void)
{
    pp_image_slot_info_t info;
    CHECK(!pp_image_slot_parse(NULL, &info));
    CHECK(!pp_image_slot_parse("", &info));
    CHECK(!pp_image_slot_parse("games/", &info));
    CHECK(!pp_image_slot_parse("games/G-AAAA", &info));
    CHECK(!pp_image_slot_parse("games/../avatar", &info));
    CHECK(!pp_image_slot_parse("games/g-a1b2/x", &info));
    CHECK(!pp_image_slot_parse("other", &info));
    CHECK(!pp_image_slot_parse("qr_wechat", &info));
    CHECK(!pp_image_slot_parse("avatar/extra", &info));
    CHECK(!pp_image_slot_path(NULL, NULL, 0));
    char small[8];
    CHECK(pp_image_slot_parse("avatar", &info));
    CHECK(!pp_image_slot_path(&info, small, sizeof(small)));
}

static void test_convenience_paths(void)
{
    char rel[PP_STORAGE_PATH_MAX];
    CHECK(pp_image_avatar_path(rel, sizeof(rel)));
    CHECK_STR_EQ(rel, "img/avatar.bin");
    CHECK(pp_image_cover_path("g-a1b2c3", rel, sizeof(rel)));
    CHECK_STR_EQ(rel, "img/games/g-a1b2c3.bin");
    CHECK(!pp_image_cover_path("bad id", rel, sizeof(rel)));
}

int main(void)
{
    test_named_slots();
    test_game_cover_slot();
    test_invalid_slots();
    test_convenience_paths();
    return pp_test_result("test_image_store");
}
