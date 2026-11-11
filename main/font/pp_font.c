#include "font/pp_font.h"

#include <string.h>

#include "app/text_utf8.h"
#include "font/pp_font_charset.h"

bool pp_font_covers(uint32_t codepoint)
{
    if (codepoint == '\n') return true;
    size_t low = 0;
    size_t high = pp_font_charset_range_count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        const pp_font_range_t *range = &pp_font_charset_ranges[mid];
        if (codepoint < range->first) {
            high = mid;
        } else if (codepoint > range->last) {
            low = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

bool pp_font_first_missing(const char *text, uint32_t *missing)
{
    if (!text) return false;
    const char *cursor = text;
    while (*cursor) {
        const char *before = cursor;
        const uint32_t cp = pp_text_next(&cursor);
        if (cp == 0 || cp == '\n') continue;
        if (cp == PP_CODEPOINT_INVALID || !pp_font_covers(cp)) {
            if (missing) *missing = cp;
            (void)before;
            return true;
        }
    }
    return false;
}

bool pp_font_text_covered(const char *text)
{
    return !pp_font_first_missing(text, NULL);
}

void pp_font_sanitize(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;
    size_t out = 0;
    const char *cursor = src;
    while (*cursor) {
        const char *before = cursor;
        const uint32_t cp = pp_text_next(&cursor);
        if (cp == 0) break;
        if (cp == PP_CODEPOINT_INVALID || (!pp_font_covers(cp) && cp != '\n')) {
            if (out + 1 >= dst_size) break;
            dst[out++] = '?';
            continue;
        }
        const size_t bytes = (size_t)(cursor - before);
        if (out + bytes >= dst_size) break;
        memcpy(dst + out, before, bytes);
        out += bytes;
    }
    dst[out] = '\0';
}

size_t pp_font_charset_count(void)
{
    return pp_font_charset_range_count;
}

bool pp_font_charset_range(size_t index, uint32_t *first, uint32_t *last)
{
    if (index >= pp_font_charset_range_count) return false;
    if (first) *first = pp_font_charset_ranges[index].first;
    if (last) *last = pp_font_charset_ranges[index].last;
    return true;
}
