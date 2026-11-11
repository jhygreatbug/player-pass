#include "data/data_validate.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app/text_utf8.h"

#define PP_URL_MAX_BYTES 480

bool pp_field_valid(const char *text, size_t max_codepoints,
                    bool allow_newline)
{
    if (!text) return false;
    if (!pp_text_valid(text)) return false;
    if (pp_text_has_control(text, allow_newline)) return false;
    const size_t count = pp_text_count(text);
    return count != SIZE_MAX && count <= max_codepoints;
}

bool pp_url_valid(const char *url)
{
    if (!url || url[0] == '\0') return false;
    const size_t length = strlen(url);
    if (length > PP_URL_MAX_BYTES) return false;
    if (!pp_text_valid(url)) return false;
    if (!(strncmp(url, "http://", 7) == 0 ||
          strncmp(url, "https://", 8) == 0)) {
        return false;
    }
    const char *host = strstr(url, "://");
    if (!host) return false;
    host += 3;
    if (*host == '\0' || *host == '/') return false;

    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c == 0x7F) return false;
        if (c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') {
            return false;
        }
    }
    return true;
}

bool pp_optional_url_valid(const char *url)
{
    if (!url || url[0] == '\0') return true;
    return pp_url_valid(url);
}

void pp_set_error(char *error, size_t error_size, const char *format, ...)
{
    if (!error || error_size == 0) return;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(error, error_size, format, args);
    va_end(args);
}
