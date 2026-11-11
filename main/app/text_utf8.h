// UTF-8 validation, counting, and codepoint-safe copying.
//
// Device text is stored and displayed as UTF-8.  Truncating by bytes can cut a
// multibyte character in half and leaves invalid text behind, so every fixed
// buffer boundary in the application goes through this module.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PP_CODEPOINT_INVALID 0xFFFDu

typedef enum {
    PP_TEXT_OK = 0,
    PP_TEXT_INVALID_UTF8,
    PP_TEXT_TOO_LONG,
    PP_TEXT_TOO_SMALL,
} pp_text_result_t;

// Strictly validate one NUL-terminated UTF-8 string.  Rejects overlong forms,
// UTF-16 surrogates, code points above U+10FFFF, and truncated sequences.
bool pp_text_valid(const char *text);

// Number of Unicode code points, or SIZE_MAX when the input is invalid.
size_t pp_text_count(const char *text);

// Decode the next code point and advance *cursor.  Invalid bytes yield
// PP_CODEPOINT_INVALID and advance one byte, so scanning always terminates.
uint32_t pp_text_next(const char **cursor);

// Copy at most max_codepoints into dst (NUL-terminated).  Returns TOO_LONG when
// src contains more code points than allowed; dst is then left empty.  Returns
// TOO_SMALL when the destination cannot hold even the allowed prefix.
pp_text_result_t pp_text_copy(char *dst, size_t dst_size, const char *src,
                              size_t max_codepoints);

// Truncate for display: at most max_codepoints, with U+2026 appended when the
// input was longer and the destination has room.  Invalid input becomes an
// empty string.  This is the only truncation path used when rendering dynamic
// content.
void pp_text_copy_display(char *dst, size_t dst_size, const char *src,
                          size_t max_codepoints);

// True when text contains a C0/C1 control character other than '\n' when
// allow_newline is true.  Used by API validation so stored text cannot carry
// terminal escape bytes or embedded NULs.
bool pp_text_has_control(const char *text, bool allow_newline);

// ASCII-only helpers for ids and tokens.
bool pp_text_is_ascii_id(const char *text, size_t min_len, size_t max_len);
