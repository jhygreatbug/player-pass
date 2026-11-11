#include "storage/storage_budget.h"

#define PP_UPDATE_RESERVE_BYTES (4u * 1024u)

int pp_storage_percent(uint64_t used, uint64_t total)
{
    if (total == 0) return 100;
    if (used >= total) return 100;
    return (int)(used * 100u / total);
}

pp_storage_level_t pp_storage_level(uint64_t used, uint64_t total)
{
    if (total == 0) return PP_STORAGE_LEVEL_CRITICAL;
    const uint64_t percent = used * 100u / total;
    if (percent >= PP_STORAGE_CRITICAL_PERCENT) return PP_STORAGE_LEVEL_CRITICAL;
    if (percent >= PP_STORAGE_WARN_PERCENT) return PP_STORAGE_LEVEL_WARN;
    return PP_STORAGE_LEVEL_OK;
}

bool pp_storage_can_fit(uint64_t free_bytes, uint64_t new_bytes,
                        uint64_t reserve_bytes)
{
    if (new_bytes > UINT64_MAX - reserve_bytes) return false;
    return free_bytes >= new_bytes + reserve_bytes;
}

uint64_t pp_storage_reserve_bytes(void)
{
    return PP_UPDATE_RESERVE_BYTES;
}
