// Generated character coverage table for the device fonts.
//
// Emitted by tools/gen_fonts.py from the same inventory used to build
// pp_font_12.c and pp_font_24.c.  The runtime sanitizer and the web API use
// this table to reject unsupported characters instead of silently drawing a
// placeholder.
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t first;
    uint32_t last;
} pp_font_range_t;

extern const pp_font_range_t pp_font_charset_ranges[];
extern const size_t pp_font_charset_range_count;
