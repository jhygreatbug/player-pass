// Storage warning thresholds shared by the device UI and the web API.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PP_STORAGE_LEVEL_OK = 0,
    PP_STORAGE_LEVEL_WARN,
    PP_STORAGE_LEVEL_CRITICAL,
} pp_storage_level_t;

#define PP_STORAGE_WARN_PERCENT 80
#define PP_STORAGE_CRITICAL_PERCENT 90

// Integer percentage, clamped to 0..100.  A zero-sized filesystem is treated as
// full so callers never divide by zero.
int pp_storage_percent(uint64_t used, uint64_t total);

// 80% (inclusive) is a warning; 90% (inclusive) is critical.
pp_storage_level_t pp_storage_level(uint64_t used, uint64_t total);

// True when a new file of new_bytes can be written while the old copy still
// exists (the swap needs both at once) plus the reserve.
bool pp_storage_can_fit(uint64_t free_bytes, uint64_t new_bytes,
                        uint64_t reserve_bytes);
uint64_t pp_storage_reserve_bytes(void);
