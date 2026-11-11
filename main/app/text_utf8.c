#include "app/text_utf8.h"

#include <string.h>

static size_t sequence_length(uint8_t lead)
{
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

// Decode one sequence.  Returns false for invalid/truncated input and leaves
// *advance at the number of bytes to skip (1 for an invalid lead byte).
static bool decode_one(const uint8_t *s, size_t available, uint32_t *cp,
                       size_t *advance)
{
    const uint8_t lead = s[0];
    const size_t len = sequence_length(lead);
    *advance = 1;
    if (len == 0 || len > available) return false;

    uint32_t value;
    switch (len) {
    case 1:
        value = lead;
        break;
    case 2:
        value = lead & 0x1Fu;
        break;
    case 3:
        value = lead & 0x0Fu;
        break;
    default:
        value = lead & 0x07u;
        break;
    }
    for (size_t i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) return false;
        value = (value << 6) | (s[i] & 0x3Fu);
    }

    // Reject overlong encodings and surrogate halves.
    static const uint32_t minimum[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (value < minimum[len]) return false;
    if (value >= 0xD800 && value <= 0xDFFF) return false;
    if (value > 0x10FFFF) return false;

    *cp = value;
    *advance = len;
    return true;
}

bool pp_text_valid(const char *text)
{
    if (!text) return false;
    const uint8_t *s = (const uint8_t *)text;
    const size_t length = strlen(text);
    size_t offset = 0;
    while (offset < length) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset, length - offset, &cp, &advance)) return false;
        offset += advance;
    }
    return true;
}

size_t pp_text_count(const char *text)
{
    if (!text) return SIZE_MAX;
    const size_t length = strlen(text);
    const uint8_t *s = (const uint8_t *)text;
    size_t offset = 0;
    size_t count = 0;
    while (offset < length) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset, length - offset, &cp, &advance)) return SIZE_MAX;
        offset += advance;
        ++count;
    }
    return count;
}

uint32_t pp_text_next(const char **cursor)
{
    if (!cursor || !*cursor || **cursor == '\0') return 0;
    const uint8_t *s = (const uint8_t *)*cursor;
    const size_t length = strlen(*cursor);
    uint32_t cp;
    size_t advance;
    if (!decode_one(s, length, &cp, &advance)) {
        *cursor += 1;
        return PP_CODEPOINT_INVALID;
    }
    *cursor += advance;
    return cp;
}

pp_text_result_t pp_text_copy(char *dst, size_t dst_size, const char *src,
                              size_t max_codepoints)
{
    if (!dst || dst_size == 0) return PP_TEXT_TOO_SMALL;
    dst[0] = '\0';
    if (!src) return PP_TEXT_INVALID_UTF8;
    if (!pp_text_valid(src)) return PP_TEXT_INVALID_UTF8;

    const size_t length = strlen(src);
    const uint8_t *s = (const uint8_t *)src;
    size_t offset = 0;
    size_t out = 0;
    size_t count = 0;
    while (offset < length) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset, length - offset, &cp, &advance)) {
            dst[0] = '\0';
            return PP_TEXT_INVALID_UTF8;
        }
        if (count == max_codepoints) {
            dst[0] = '\0';
            return PP_TEXT_TOO_LONG;
        }
        if (out + advance >= dst_size) {
            dst[0] = '\0';
            return PP_TEXT_TOO_SMALL;
        }
        memcpy(dst + out, s + offset, advance);
        out += advance;
        offset += advance;
        ++count;
    }
    dst[out] = '\0';
    return PP_TEXT_OK;
}

static void copy_prefix(char *dst, size_t dst_size, const uint8_t *s,
                        size_t length, size_t limit)
{
    size_t offset = 0;
    size_t out = 0;
    size_t count = 0;
    while (offset < length && count < limit) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset, length - offset, &cp, &advance)) break;
        if (out + advance >= dst_size) break;
        memcpy(dst + out, s + offset, advance);
        out += advance;
        offset += advance;
        ++count;
    }
    dst[out] = '\0';
}

void pp_text_copy_display(char *dst, size_t dst_size, const char *src,
                          size_t max_codepoints)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    const size_t length = strlen(src);
    const uint8_t *s = (const uint8_t *)src;
    size_t offset = 0;
    size_t count = 0;
    bool invalid = false;
    while (offset < length) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset, length - offset, &cp, &advance)) {
            invalid = true;
            break;
        }
        offset += advance;
        ++count;
    }
    if (invalid) return;

    if (count <= max_codepoints) {
        copy_prefix(dst, dst_size, s, length, max_codepoints);
        return;
    }

    // Reserve three bytes for the ellipsis (U+2026) plus the terminator.
    const size_t ellipsis_bytes = 3;
    if (max_codepoints == 0 || dst_size <= ellipsis_bytes + 1) {
        copy_prefix(dst, dst_size, s, length, max_codepoints);
        return;
    }
    // Count bytes of the longest prefix that fits *and* leaves ellipsis room.
    size_t offset2 = 0;
    size_t out = 0;
    size_t kept = 0;
    while (offset2 < length && kept < max_codepoints) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset2, length - offset2, &cp, &advance)) break;
        if (out + advance + ellipsis_bytes >= dst_size) break;
        memcpy(dst + out, s + offset2, advance);
        out += advance;
        offset2 += advance;
        ++kept;
    }
    memcpy(dst + out, "\xE2\x80\xA6", ellipsis_bytes);
    dst[out + ellipsis_bytes] = '\0';
}

bool pp_text_has_control(const char *text, bool allow_newline)
{
    if (!text) return true;
    const uint8_t *s = (const uint8_t *)text;
    const size_t length = strlen(text);
    size_t offset = 0;
    while (offset < length) {
        uint32_t cp;
        size_t advance;
        if (!decode_one(s + offset, length - offset, &cp, &advance)) return true;
        if ((cp < 0x20 && !(allow_newline && cp == '\n')) || cp == 0x7F) return true;
        if (cp >= 0x80 && cp <= 0x9F) return true;
        offset += advance;
    }
    return false;
}

bool pp_text_is_ascii_id(const char *text, size_t min_len, size_t max_len)
{
    if (!text) return false;
    const size_t length = strlen(text);
    if (length < min_len || length > max_len) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}
