// Minimal assertion helpers for the portable host tests.
#pragma once

#include <stdio.h>

static int pp_test_failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                             \
            ++pp_test_failures;                                              \
        }                                                                    \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                       \
    do {                                                                     \
        const char *pp_actual_ = (actual);                                   \
        const char *pp_expected_ = (expected);                               \
        if (!pp_actual_ || !pp_expected_ || strcmp(pp_actual_, pp_expected_) != 0) { \
            fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,      \
                    __LINE__, pp_actual_ ? pp_actual_ : "(null)",            \
                    pp_expected_ ? pp_expected_ : "(null)");                 \
            ++pp_test_failures;                                              \
        }                                                                    \
    } while (0)

static inline int pp_test_result(const char *name)
{
    if (pp_test_failures == 0) {
        printf("%s: PASS\n", name);
        return 0;
    }
    fprintf(stderr, "%s: %d failure(s)\n", name, pp_test_failures);
    return 1;
}
