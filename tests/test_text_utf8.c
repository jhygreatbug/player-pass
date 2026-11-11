#include <string.h>

#include "app/text_utf8.h"
#include "pp_test.h"

static void test_valid_and_count(void)
{
    CHECK(pp_text_valid(""));
    CHECK(pp_text_valid("ABC 123"));
    CHECK(pp_text_valid("\xE4\xB8\xAD\xE6\x96\x87"));
    CHECK(pp_text_valid("\xF0\x9F\x8E\xAE"));
    CHECK(pp_text_count("") == 0);
    CHECK(pp_text_count("ABC") == 3);
    CHECK(pp_text_count("\xE4\xB8\xAD\xE6\x96\x87") == 2);
    CHECK(pp_text_count("\xF0\x9F\x8E\xAE") == 1);

    CHECK(!pp_text_valid("\xC0\xAF"));             // overlong '/'
    CHECK(!pp_text_valid("\xED\xA0\x80"));         // surrogate
    CHECK(!pp_text_valid("\xF4\x90\x80\x80"));     // > U+10FFFF
    CHECK(!pp_text_valid("\xE4\xB8"));             // truncated
    CHECK(!pp_text_valid("\x80"));                 // stray continuation
    CHECK(pp_text_count("\xE4\xB8") == SIZE_MAX);
}

static void test_copy(void)
{
    char out[32];
    CHECK(pp_text_copy(out, sizeof(out), "hello", 10) == PP_TEXT_OK);
    CHECK_STR_EQ(out, "hello");
    CHECK(pp_text_copy(out, sizeof(out), "hello", 5) == PP_TEXT_OK);
    CHECK(pp_text_copy(out, sizeof(out), "hello", 4) == PP_TEXT_TOO_LONG);
    CHECK_STR_EQ(out, "");
    CHECK(pp_text_copy(out, 4, "hello", 10) == PP_TEXT_TOO_SMALL);
    CHECK(pp_text_copy(out, sizeof(out), "\xE4\xB8\xAD\xE6\x96\x87", 2) ==
          PP_TEXT_OK);
    CHECK_STR_EQ(out, "\xE4\xB8\xAD\xE6\x96\x87");
    CHECK(pp_text_copy(out, sizeof(out), "\xE4\xB8\xAD\xE6\x96\x87", 1) ==
          PP_TEXT_TOO_LONG);
}

static void test_display_truncate(void)
{
    char out[32];
    // 5 Chinese characters truncate to 3 + U+2026.
    pp_text_copy_display(out, sizeof(out),
                         "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x9B\x9B\xE4\xBA\x94",
                         3);
    CHECK_STR_EQ(out, "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE2\x80\xA6");
    CHECK(pp_text_valid(out));

    pp_text_copy_display(out, sizeof(out), "abc", 10);
    CHECK_STR_EQ(out, "abc");

    // No room for the ellipsis: still cut on a code-point boundary.
    pp_text_copy_display(out, 5, "\xE4\xB8\x80\xE4\xBA\x8C", 2);
    CHECK(pp_text_valid(out));
    CHECK(strlen(out) <= 4);

    pp_text_copy_display(out, sizeof(out), "\xE4\xB8", 5);
    CHECK_STR_EQ(out, "");
}

static void test_controls_and_ids(void)
{
    CHECK(!pp_text_has_control("plain text", false));
    CHECK(pp_text_has_control("line\nbreak", false));
    CHECK(!pp_text_has_control("line\nbreak", true));
    CHECK(pp_text_has_control("bell\x07", true));
    CHECK(pp_text_has_control("del\x7F", false));
    CHECK(!pp_text_has_control("\xE4\xB8\xAD", false));

    CHECK(pp_text_is_ascii_id("abc-123", 4, 24));
    CHECK(!pp_text_is_ascii_id("ab", 4, 24));
    CHECK(!pp_text_is_ascii_id("abc 123", 4, 24));
    CHECK(!pp_text_is_ascii_id("abc/123", 4, 24));
    CHECK(!pp_text_is_ascii_id("abc.123", 4, 24));
}

int main(void)
{
    test_valid_and_count();
    test_copy();
    test_display_truncate();
    test_controls_and_ids();
    return pp_test_result("test_text_utf8");
}
