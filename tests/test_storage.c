#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pp_test.h"
#include "storage/storage.h"

static char s_root[128];

static void remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir) {
        (void)remove(path);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char child[512];
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_tree(child);
        } else {
            (void)remove(child);
        }
    }
    closedir(dir);
    (void)rmdir(path);
}

static bool write_text(const char *rel, const char *text)
{
    pp_storage_t storage;
    if (pp_storage_init(&storage, s_root) != PP_STORAGE_OK) return false;
    return pp_storage_write_atomic(&storage, rel, text, strlen(text)) ==
           PP_STORAGE_OK;
}

static bool read_text(const char *rel, char *out, size_t out_size)
{
    pp_storage_t storage;
    if (pp_storage_init(&storage, s_root) != PP_STORAGE_OK) return false;
    size_t length = 0;
    return pp_storage_read(&storage, rel, out, out_size, &length) ==
           PP_STORAGE_OK;
}

static void test_paths(void)
{
    CHECK(pp_storage_path_valid("data/profile.json"));
    CHECK(pp_storage_path_valid("img/games/g-abc1.bin"));
    CHECK(pp_storage_path_valid("data"));
    CHECK(pp_storage_path_valid("img"));
    CHECK(!pp_storage_path_valid("/data/profile.json"));
    CHECK(!pp_storage_path_valid("data/../profile.json"));
    CHECK(!pp_storage_path_valid("data//profile.json"));
    CHECK(!pp_storage_path_valid("data/"));
    CHECK(!pp_storage_path_valid("other/file"));
    CHECK(!pp_storage_path_valid("data/file name"));
    CHECK(!pp_storage_path_valid("data/file\\name"));
    CHECK(!pp_storage_path_valid(""));
    CHECK(!pp_storage_path_valid(NULL));
}

static void test_write_and_replace(void)
{
    CHECK(write_text("data/profile.json", "one"));
    char buffer[64];
    CHECK(read_text("data/profile.json", buffer, sizeof(buffer)));
    CHECK_STR_EQ(buffer, "one");

    CHECK(write_text("data/profile.json", "two"));
    CHECK(read_text("data/profile.json", buffer, sizeof(buffer)));
    CHECK_STR_EQ(buffer, "two");

    bool exists = false;
    pp_storage_t storage;
    CHECK(pp_storage_init(&storage, s_root) == PP_STORAGE_OK);
    CHECK(pp_storage_exists(&storage, "data/profile.json.old", &exists) ==
          PP_STORAGE_OK);
    CHECK(!exists);
    CHECK(pp_storage_exists(&storage, "data/profile.json.tmp", &exists) ==
          PP_STORAGE_OK);
    CHECK(!exists);

    size_t size = 0;
    CHECK(pp_storage_size(&storage, "data/profile.json", &size) ==
          PP_STORAGE_OK);
    CHECK(size == 3);
    CHECK(pp_storage_remove(&storage, "data/profile.json") == PP_STORAGE_OK);
    CHECK(pp_storage_remove(&storage, "data/profile.json") ==
          PP_STORAGE_ERR_NOT_FOUND);
}

static void test_streaming(void)
{
    CHECK(write_text("img/avatar.bin", "old-avatar"));
    pp_storage_t storage;
    CHECK(pp_storage_init(&storage, s_root) == PP_STORAGE_OK);

    pp_storage_temp_t temp;
    CHECK(pp_storage_begin_temp(&storage, "img/avatar.bin", &temp) ==
          PP_STORAGE_OK);
    CHECK(pp_storage_write_temp(&temp, "partial", 7) == PP_STORAGE_OK);
    pp_storage_abort_temp(&storage, &temp);
    char buffer[64];
    CHECK(read_text("img/avatar.bin", buffer, sizeof(buffer)));
    CHECK_STR_EQ(buffer, "old-avatar");

    CHECK(pp_storage_begin_temp(&storage, "img/avatar.bin", &temp) ==
          PP_STORAGE_OK);
    CHECK(pp_storage_write_temp(&temp, "new-avatar", 10) == PP_STORAGE_OK);
    CHECK(pp_storage_commit_temp(&storage, &temp) == PP_STORAGE_OK);
    CHECK(read_text("img/avatar.bin", buffer, sizeof(buffer)));
    CHECK_STR_EQ(buffer, "new-avatar");

    bool exists = false;
    CHECK(pp_storage_exists(&storage, "img/avatar.bin.tmp", &exists) ==
          PP_STORAGE_OK);
    CHECK(!exists);
}

static void test_recovery(void)
{
    pp_storage_t storage;
    CHECK(pp_storage_init(&storage, s_root) == PP_STORAGE_OK);

    // Interrupted swap: the old copy exists but the final name is gone.
    CHECK(pp_storage_write_atomic(&storage, "data/games.json", "stable", 6) ==
          PP_STORAGE_OK);
    char final_path[512];
    char old_path[512];
    char tmp_path[512];
    snprintf(final_path, sizeof(final_path), "%s/data/games.json", s_root);
    snprintf(old_path, sizeof(old_path), "%s/data/games.json.old", s_root);
    snprintf(tmp_path, sizeof(tmp_path), "%s/data/games.json.tmp", s_root);
    CHECK(rename(final_path, old_path) == 0);
    FILE *placeholder = fopen(tmp_path, "wb");
    CHECK(placeholder != NULL);
    if (placeholder) fclose(placeholder);

    // Reinitializing recovers the old copy and discards the temporary file.
    CHECK(pp_storage_init(&storage, s_root) == PP_STORAGE_OK);
    char buffer[64];
    CHECK(read_text("data/games.json", buffer, sizeof(buffer)));
    CHECK_STR_EQ(buffer, "stable");

    // A completed final file wins over a leftover old copy.
    CHECK(write_text("data/games.json.old", "stale"));
    CHECK(pp_storage_init(&storage, s_root) == PP_STORAGE_OK);
    struct stat st;
    CHECK(stat(old_path, &st) != 0);
    CHECK(read_text("data/games.json", buffer, sizeof(buffer)));
    CHECK_STR_EQ(buffer, "stable");
}

static void test_usage(void)
{
    pp_storage_t storage;
    CHECK(pp_storage_init(&storage, s_root) == PP_STORAGE_OK);
    uint64_t used = 0;
    uint64_t total = 0;
    CHECK(pp_storage_usage(&storage, &used, &total) == PP_STORAGE_OK);
    CHECK(total > 0);
    CHECK(used <= total);
}

int main(void)
{
    char template[] = "/tmp/pp-storage-test.XXXXXX";
    char *dir = mkdtemp(template);
    CHECK(dir != NULL);
    if (!dir) return pp_test_result("test_storage");
    snprintf(s_root, sizeof(s_root), "%s", dir);

    test_paths();
    test_write_and_replace();
    test_streaming();
    test_recovery();
    test_usage();

    remove_tree(s_root);
    return pp_test_result("test_storage");
}
