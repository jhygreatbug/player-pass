#include "storage/storage.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "esp_littlefs.h"
#else
#include <sys/statvfs.h>
#endif


static bool component_valid(const char *start, size_t length)
{
    return length > 0 && !(length == 1 && start[0] == '.') &&
           !(length == 2 && start[0] == '.' && start[1] == '.');
}

bool pp_storage_path_valid(const char *rel)
{
    if (!rel) return false;
    const size_t length = strlen(rel);
    if (length == 0 || length >= PP_STORAGE_PATH_MAX) return false;
    if (rel[0] == '/' || rel[length - 1] == '/') return false;
    if (strcmp(rel, "data") == 0 || strcmp(rel, "img") == 0) return true;

    const bool in_data = strncmp(rel, "data/", 5) == 0 && length > 5;
    const bool in_img = strncmp(rel, "img/", 4) == 0 && length > 4;
    if (!in_data && !in_img) return false;

    size_t component_start = 0;
    for (size_t i = 0; i <= length; ++i) {
        const char c = (i == length) ? '/' : rel[i];
        if (c == '/') {
            if (!component_valid(rel + component_start, i - component_start)) {
                return false;
            }
            component_start = i + 1;
            continue;
        }
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                        c == '-';
        if (!ok) return false;
    }
    return true;
}

static bool join_path(const pp_storage_t *storage, const char *rel,
                      char *out, size_t out_size)
{
    if (!storage || !storage->ready || !pp_storage_path_valid(rel)) return false;
    const int written = snprintf(out, out_size, "%s/%s", storage->root, rel);
    return written > 0 && (size_t)written < out_size;
}

bool pp_storage_abs_path(const pp_storage_t *storage, const char *rel,
                         char *out, size_t out_size)
{
    return join_path(storage, rel, out, out_size);
}

static bool temp_path(const char *rel, const char *suffix, char *out,
                      size_t out_size)
{
    const int written = snprintf(out, out_size, "%s%s", rel, suffix);
    return written > 0 && (size_t)written < out_size;
}

static bool make_dir(const pp_storage_t *storage, const char *rel)
{
    char path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, rel, path, sizeof(path))) return false;
    if (mkdir(path, 0775) == 0) return true;
    return errno == EEXIST;
}

static void cleanup_dir(const pp_storage_t *storage, const char *dir_rel)
{
    char dir_path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, dir_rel, dir_path, sizeof(dir_path))) return;
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        const size_t name_len = strlen(name);
        const bool is_tmp = name_len > 4 && strcmp(name + name_len - 4, ".tmp") == 0;
        const bool is_old = name_len > 4 && strcmp(name + name_len - 4, ".old") == 0;
        if (!is_tmp && !is_old) continue;

        char file_rel[PP_STORAGE_PATH_MAX];
        const int rel_len = snprintf(file_rel, sizeof(file_rel), "%s/%s", dir_rel, name);
        if (rel_len <= 0 || (size_t)rel_len >= sizeof(file_rel)) continue;

        char file_path[PP_STORAGE_PATH_MAX + 128];
        if (!join_path(storage, file_rel, file_path, sizeof(file_path))) continue;

        if (is_tmp) {
            (void)remove(file_path);
            continue;
        }

        char final_rel[PP_STORAGE_PATH_MAX];
        memcpy(final_rel, file_rel, (size_t)rel_len - 3); // strip ".old"
        final_rel[(size_t)rel_len - 4] = '\0';
        char final_path[PP_STORAGE_PATH_MAX + 128];
        if (!join_path(storage, final_rel, final_path, sizeof(final_path))) {
            (void)remove(file_path);
            continue;
        }
        struct stat st;
        if (stat(final_path, &st) != 0) {
            (void)rename(file_path, final_path);
        } else {
            (void)remove(file_path);
        }
    }
    closedir(dir);
}

pp_storage_err_t pp_storage_init(pp_storage_t *storage, const char *root)
{
    if (!storage || !root || root[0] == '\0') return PP_STORAGE_ERR_INVALID;
    const int written = snprintf(storage->root, sizeof(storage->root), "%s", root);
    if (written <= 0 || (size_t)written >= sizeof(storage->root)) {
        return PP_STORAGE_ERR_INVALID;
    }
    // Strip a trailing slash so joins stay unambiguous.
    size_t len = strlen(storage->root);
    while (len > 1 && storage->root[len - 1] == '/') {
        storage->root[--len] = '\0';
    }
    storage->ready = true;

    struct stat st;
    if (stat(storage->root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        storage->ready = false;
        return PP_STORAGE_ERR_NOT_FOUND;
    }
    const char *dirs[] = { "data", "img", "img/games" };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (!make_dir(storage, dirs[i])) {
            storage->ready = false;
            return PP_STORAGE_ERR_IO;
        }
    }
    cleanup_dir(storage, "data");
    cleanup_dir(storage, "img");
    cleanup_dir(storage, "img/games");
    return PP_STORAGE_OK;
}

const char *pp_storage_error_name(pp_storage_err_t error)
{
    switch (error) {
    case PP_STORAGE_OK: return "ok";
    case PP_STORAGE_ERR_INVALID: return "invalid-path";
    case PP_STORAGE_ERR_IO: return "io-error";
    case PP_STORAGE_ERR_NOT_FOUND: return "not-found";
    case PP_STORAGE_ERR_TOO_BIG: return "too-big";
    case PP_STORAGE_ERR_NO_SPACE: return "no-space";
    case PP_STORAGE_ERR_NO_MEMORY: return "no-memory";
    default: return "unknown";
    }
}

pp_storage_err_t pp_storage_exists(const pp_storage_t *storage, const char *rel,
                                   bool *exists)
{
    if (!exists) return PP_STORAGE_ERR_INVALID;
    *exists = false;
    char path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, rel, path, sizeof(path))) return PP_STORAGE_ERR_INVALID;
    struct stat st;
    if (stat(path, &st) == 0) {
        *exists = S_ISREG(st.st_mode);
        return PP_STORAGE_OK;
    }
    if (errno == ENOENT) return PP_STORAGE_OK;
    return PP_STORAGE_ERR_IO;
}

pp_storage_err_t pp_storage_size(const pp_storage_t *storage, const char *rel,
                                 size_t *size)
{
    if (!size) return PP_STORAGE_ERR_INVALID;
    char path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, rel, path, sizeof(path))) return PP_STORAGE_ERR_INVALID;
    struct stat st;
    if (stat(path, &st) != 0) {
        return errno == ENOENT ? PP_STORAGE_ERR_NOT_FOUND : PP_STORAGE_ERR_IO;
    }
    if (!S_ISREG(st.st_mode)) return PP_STORAGE_ERR_INVALID;
    *size = (size_t)st.st_size;
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_storage_read(const pp_storage_t *storage, const char *rel,
                                 void *buffer, size_t capacity, size_t *length)
{
    if (!buffer || capacity == 0) return PP_STORAGE_ERR_INVALID;
    size_t size = 0;
    pp_storage_err_t err = pp_storage_size(storage, rel, &size);
    if (err != PP_STORAGE_OK) return err;
    if (size >= capacity) return PP_STORAGE_ERR_TOO_BIG;

    char path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, rel, path, sizeof(path))) return PP_STORAGE_ERR_INVALID;
    FILE *fp = fopen(path, "rb");
    if (!fp) return PP_STORAGE_ERR_IO;
    const size_t got = fread(buffer, 1, size, fp);
    const bool failed = ferror(fp) != 0;
    (void)fclose(fp);
    if (failed || got != size) return PP_STORAGE_ERR_IO;
    ((uint8_t *)buffer)[size] = '\0';
    if (length) *length = size;
    return PP_STORAGE_OK;
}

static pp_storage_err_t replace_file(const pp_storage_t *storage,
                                     const char *rel, const char *tmp_rel)
{
    char final_path[PP_STORAGE_PATH_MAX + 128];
    char tmp_path[PP_STORAGE_PATH_MAX + 128];
    char old_rel[PP_STORAGE_PATH_MAX];
    char old_path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, rel, final_path, sizeof(final_path)) ||
        !join_path(storage, tmp_rel, tmp_path, sizeof(tmp_path)) ||
        !temp_path(rel, ".old", old_rel, sizeof(old_rel)) ||
        !join_path(storage, old_rel, old_path, sizeof(old_path))) {
        return PP_STORAGE_ERR_INVALID;
    }

    struct stat st;
    const bool have_old = stat(final_path, &st) == 0;
    if (have_old) {
        (void)remove(old_path);
        if (rename(final_path, old_path) != 0) return PP_STORAGE_ERR_IO;
    }
    if (rename(tmp_path, final_path) != 0) {
        if (have_old) (void)rename(old_path, final_path);
        (void)remove(tmp_path);
        return PP_STORAGE_ERR_IO;
    }
    if (have_old) (void)remove(old_path);
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_storage_begin_temp(const pp_storage_t *storage,
                                       const char *rel, pp_storage_temp_t *temp)
{
    if (!temp || !pp_storage_path_valid(rel)) return PP_STORAGE_ERR_INVALID;
    memset(temp, 0, sizeof(*temp));
    const int written = snprintf(temp->final_rel, sizeof(temp->final_rel), "%s", rel);
    if (written <= 0 || (size_t)written >= sizeof(temp->final_rel)) {
        return PP_STORAGE_ERR_INVALID;
    }
    if (!temp_path(rel, ".tmp", temp->tmp_rel, sizeof(temp->tmp_rel))) {
        return PP_STORAGE_ERR_INVALID;
    }
    char tmp_path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, temp->tmp_rel, tmp_path, sizeof(tmp_path))) {
        return PP_STORAGE_ERR_INVALID;
    }
    temp->fp = fopen(tmp_path, "wb");
    if (!temp->fp) return PP_STORAGE_ERR_IO;
    temp->open = true;
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_storage_write_temp(pp_storage_temp_t *temp,
                                       const void *data, size_t length)
{
    if (!temp || !temp->open || !temp->fp) return PP_STORAGE_ERR_INVALID;
    if (length == 0) return PP_STORAGE_OK;
    if (fwrite(data, 1, length, temp->fp) != length) return PP_STORAGE_ERR_IO;
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_storage_commit_temp(const pp_storage_t *storage,
                                        pp_storage_temp_t *temp)
{
    if (!temp || !temp->open || !temp->fp) return PP_STORAGE_ERR_INVALID;
    bool failed = fflush(temp->fp) != 0;
    if (!failed) failed = fsync(fileno(temp->fp)) != 0;
    if (fclose(temp->fp) != 0) failed = true;
    temp->fp = NULL;
    temp->open = false;
    if (failed) {
        pp_storage_abort_temp(storage, temp);
        return PP_STORAGE_ERR_IO;
    }
    return replace_file(storage, temp->final_rel, temp->tmp_rel);
}

void pp_storage_abort_temp(const pp_storage_t *storage,
                           pp_storage_temp_t *temp)
{
    if (!temp) return;
    if (temp->fp) {
        (void)fclose(temp->fp);
        temp->fp = NULL;
    }
    temp->open = false;
    if (storage && storage->ready && temp->tmp_rel[0]) {
        char path[PP_STORAGE_PATH_MAX + 128];
        if (join_path(storage, temp->tmp_rel, path, sizeof(path))) {
            (void)remove(path);
        }
    }
}

pp_storage_err_t pp_storage_write_atomic(const pp_storage_t *storage,
                                         const char *rel, const void *data,
                                         size_t length)
{
    pp_storage_temp_t temp;
    pp_storage_err_t err = pp_storage_begin_temp(storage, rel, &temp);
    if (err != PP_STORAGE_OK) return err;
    err = pp_storage_write_temp(&temp, data, length);
    if (err != PP_STORAGE_OK) {
        pp_storage_abort_temp(storage, &temp);
        return err;
    }
    return pp_storage_commit_temp(storage, &temp);
}

pp_storage_err_t pp_storage_remove(const pp_storage_t *storage, const char *rel)
{
    char path[PP_STORAGE_PATH_MAX + 128];
    if (!join_path(storage, rel, path, sizeof(path))) return PP_STORAGE_ERR_INVALID;
    if (remove(path) != 0) {
        return errno == ENOENT ? PP_STORAGE_ERR_NOT_FOUND : PP_STORAGE_ERR_IO;
    }
    return PP_STORAGE_OK;
}

pp_storage_err_t pp_storage_usage(const pp_storage_t *storage, uint64_t *used,
                                  uint64_t *total)
{
    if (!storage || !storage->ready || !used || !total) {
        return PP_STORAGE_ERR_INVALID;
    }
#ifdef ESP_PLATFORM
    size_t total_bytes = 0;
    size_t used_bytes = 0;
    esp_err_t err = esp_littlefs_info("storage", &total_bytes, &used_bytes);
    if (err != ESP_OK) return PP_STORAGE_ERR_IO;
    *used = used_bytes;
    *total = total_bytes;
    return PP_STORAGE_OK;
#else
    struct statvfs vfs;
    if (statvfs(storage->root, &vfs) != 0) return PP_STORAGE_ERR_IO;
    const uint64_t block = (uint64_t)vfs.f_frsize;
    *total = (uint64_t)vfs.f_blocks * block;
    const uint64_t free_bytes = (uint64_t)vfs.f_bavail * block;
    *used = *total > free_bytes ? *total - free_bytes : 0;
    return PP_STORAGE_OK;
#endif
}
