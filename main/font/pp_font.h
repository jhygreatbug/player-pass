// Application fonts and runtime glyph coverage.
//
// The font subset is generated from GB2312 level 1 plus every Chinese string in
// the application; the generated range table is the single source used by the
// runtime sanitizer and the web API's text validation.  A character outside the
// table is not silently trusted: labels replace it with '?' and the firmware
// logs U+XXXX, while the admin refuses to save it until the font is rebuilt.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

LV_FONT_DECLARE(pp_font_12);
LV_FONT_DECLARE(pp_font_24);

// True for code points rendered by the generated subset.  '\n' is accepted as
// a layout character even though it has no glyph.
bool pp_font_covers(uint32_t codepoint);

// Returns false and fills *missing with the first unsupported code point.
bool pp_font_first_missing(const char *text, uint32_t *missing);
bool pp_font_text_covered(const char *text);

// Copy src to dst, replacing unsupported or invalid sequences with '?'.
// Never splits a UTF-8 sequence and always NUL-terminates.
void pp_font_sanitize(const char *src, char *dst, size_t dst_size);

size_t pp_font_charset_count(void);
bool pp_font_charset_range(size_t index, uint32_t *first, uint32_t *last);
