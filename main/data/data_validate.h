// Shared field validation for stored profile and game records.
#pragma once

#include <stdbool.h>
#include <stddef.h>

bool pp_field_valid(const char *text, size_t max_codepoints,
                    bool allow_newline);

// Accepts http:// and https:// URLs only.  Rejects whitespace, control bytes,
// and characters that cannot appear literally in a URL.  Percent-encode anything
// unusual instead of widening this rule.
bool pp_url_valid(const char *url);

// Optional fields: NULL and "" are both valid.
bool pp_optional_url_valid(const char *url);

void pp_set_error(char *error, size_t error_size, const char *format, ...);
