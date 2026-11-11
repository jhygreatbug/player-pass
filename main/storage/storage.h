// Small, portable file layer shared by the firmware and host tests.
//
// The device mounts LittleFS at /lfs and this module works below that root, so
// the same POSIX calls run on the host against a temporary directory.  Every
// replacement goes through a temporary file and a rename pair; the transient
// ".old" name is restored or cleaned on the next start, which makes a power cut
// in the middle of an update recoverable without keeping user-visible history.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PP_STORAGE_PATH_MAX 128

typedef enum {
    PP_STORAGE_OK = 0,
    PP_STORAGE_ERR_INVALID,
    PP_STORAGE_ERR_IO,
    PP_STORAGE_ERR_NOT_FOUND,
    PP_STORAGE_ERR_TOO_BIG,
    PP_STORAGE_ERR_NO_SPACE,
    PP_STORAGE_ERR_NO_MEMORY,
} pp_storage_err_t;

typedef struct {
    char root[96];
    bool ready;
} pp_storage_t;

typedef struct {
    FILE *fp;
    char final_rel[PP_STORAGE_PATH_MAX];
    char tmp_rel[PP_STORAGE_PATH_MAX];
    bool open;
} pp_storage_temp_t;

// Ensure the standard directory tree exists and recover interrupted updates.
// The firmware mounts LittleFS before calling this; the host passes a temp dir.
pp_storage_err_t pp_storage_init(pp_storage_t *storage, const char *root);

const char *pp_storage_error_name(pp_storage_err_t error);

// Reject absolute paths, "..", backslashes, empty components, and names outside
// the data/ and img/ trees.  All public functions validate the relative path.
bool pp_storage_path_valid(const char *rel);

// Resolve a validated relative path to the mounted filesystem path.  Used by
// the HTTP server to stream image files without a full-size heap buffer.
bool pp_storage_abs_path(const pp_storage_t *storage, const char *rel,
                         char *out, size_t out_size);

pp_storage_err_t pp_storage_read(const pp_storage_t *storage, const char *rel,
                                 void *buffer, size_t capacity, size_t *length);
pp_storage_err_t pp_storage_exists(const pp_storage_t *storage, const char *rel,
                                   bool *exists);
pp_storage_err_t pp_storage_size(const pp_storage_t *storage, const char *rel,
                                 size_t *size);
pp_storage_err_t pp_storage_write_atomic(const pp_storage_t *storage,
                                         const char *rel, const void *data,
                                         size_t length);
pp_storage_err_t pp_storage_remove(const pp_storage_t *storage, const char *rel);

// Streaming upload path: begin writes "<rel>.tmp"; commit fsyncs it and swaps
// it into place; abort removes it and preserves the previous file.
pp_storage_err_t pp_storage_begin_temp(const pp_storage_t *storage,
                                       const char *rel, pp_storage_temp_t *temp);
pp_storage_err_t pp_storage_write_temp(pp_storage_temp_t *temp,
                                       const void *data, size_t length);
pp_storage_err_t pp_storage_commit_temp(const pp_storage_t *storage,
                                        pp_storage_temp_t *temp);
void pp_storage_abort_temp(const pp_storage_t *storage,
                           pp_storage_temp_t *temp);

// Filesystem-wide usage.  On the device this reads the LittleFS partition so
// filesystem overhead is included; the host uses statvfs.
pp_storage_err_t pp_storage_usage(const pp_storage_t *storage, uint64_t *used,
                                  uint64_t *total);
