#include <stdint.h>

#include "pp_test.h"
#include "storage/storage.h"
#include "storage/storage_budget.h"

static void test_percent(void)
{
    CHECK(pp_storage_percent(0, 1000) == 0);
    CHECK(pp_storage_percent(500, 1000) == 50);
    CHECK(pp_storage_percent(999, 1000) == 99);
    CHECK(pp_storage_percent(1000, 1000) == 100);
    CHECK(pp_storage_percent(1200, 1000) == 100);
    CHECK(pp_storage_percent(1, 0) == 100);
}

static void test_levels(void)
{
    CHECK(pp_storage_level(0, 1000) == PP_STORAGE_LEVEL_OK);
    CHECK(pp_storage_level(799, 1000) == PP_STORAGE_LEVEL_OK);
    CHECK(pp_storage_level(800, 1000) == PP_STORAGE_LEVEL_WARN);
    CHECK(pp_storage_level(899, 1000) == PP_STORAGE_LEVEL_WARN);
    CHECK(pp_storage_level(900, 1000) == PP_STORAGE_LEVEL_CRITICAL);
    CHECK(pp_storage_level(1000, 0) == PP_STORAGE_LEVEL_CRITICAL);
}

static void test_fit(void)
{
    CHECK(pp_storage_can_fit(100u * 1024u, 60u * 1024u, 4096u));
    CHECK(!pp_storage_can_fit(100u * 1024u, 100u * 1024u, 4096u));
    CHECK(pp_storage_reserve_bytes() >= 4096u);
}

int main(void)
{
    test_percent();
    test_levels();
    test_fit();
    return pp_test_result("test_storage_budget");
}
