#include <string.h>

#include "../src/cache.h"
#include "../src/handlers.h"
#include "vendor/unity.h"

void setUp(void) {}
void tearDown(void) {}

/* ---------- parse_positive_long ---------- */

static void test_parse_positive_long_accepts_simple(void) {
    long v = 0;
    TEST_ASSERT_TRUE(parse_positive_long("5", 1, &v));
    TEST_ASSERT_EQUAL_INT(5, v);

    TEST_ASSERT_TRUE(parse_positive_long("12345", 5, &v));
    TEST_ASSERT_EQUAL_INT(12345, v);
}

static void test_parse_positive_long_rejects_zero(void) {
    long v = 0;
    TEST_ASSERT_FALSE(parse_positive_long("0", 1, &v));
}

static void test_parse_positive_long_rejects_negative(void) {
    long v = 0;
    TEST_ASSERT_FALSE(parse_positive_long("-1", 2, &v));
}

static void test_parse_positive_long_rejects_non_digit(void) {
    long v = 0;
    TEST_ASSERT_FALSE(parse_positive_long("abc", 3, &v));
    TEST_ASSERT_FALSE(parse_positive_long("12x", 3, &v));
    TEST_ASSERT_FALSE(parse_positive_long(" 5", 2, &v));
}

static void test_parse_positive_long_rejects_empty(void) {
    long v = 0;
    TEST_ASSERT_FALSE(parse_positive_long("", 0, &v));
    TEST_ASSERT_FALSE(parse_positive_long(NULL, 0, &v));
}

static void test_parse_positive_long_rejects_leading_zero(void) {
    long v = 0;
    TEST_ASSERT_FALSE(parse_positive_long("05", 2, &v));
}

/* ---------- validate_content_nonempty ---------- */

static void test_validate_content_nonempty(void) {
    TEST_ASSERT_TRUE(validate_content_nonempty("hi", 2));
    TEST_ASSERT_FALSE(validate_content_nonempty("", 0));
    TEST_ASSERT_FALSE(validate_content_nonempty(NULL, 0));
}

/* ---------- json_escape ---------- */

static void test_json_escape_passthrough(void) {
    char buf[64];
    int n = json_escape("hello", 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(5, n);
    TEST_ASSERT_EQUAL_STRING("hello", buf);
}

static void test_json_escape_quotes_and_backslash(void) {
    char buf[64];
    int n = json_escape("a\"b\\c", 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c", buf);
}

static void test_json_escape_control_chars(void) {
    char buf[64];
    int n = json_escape("a\nb\tc", 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("a\\nb\\tc", buf);
}

static void test_json_escape_overflow(void) {
    char buf[4];
    int n = json_escape("hello", 5, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(-1, n);
}

/* ---------- API prefix / path matching ---------- */

static void test_build_api_prefix(void) {
    char buf[32];
    int n = build_api_prefix("js", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("/api/js", buf);
}

static void test_path_has_prefix(void) {
    const char *prefix = "/api/c";
    size_t plen = strlen(prefix);
    TEST_ASSERT_TRUE(path_has_prefix("/api/c", 6, prefix, plen));
    TEST_ASSERT_TRUE(path_has_prefix("/api/c/data", 11, prefix, plen));
    TEST_ASSERT_TRUE(path_has_prefix("/api/c/data/42", 14, prefix, plen));
    TEST_ASSERT_FALSE(path_has_prefix("/api/cpp/data", 13, prefix, plen));
    TEST_ASSERT_FALSE(path_has_prefix("/health", 7, prefix, plen));
    TEST_ASSERT_FALSE(path_has_prefix("/", 1, prefix, plen));
}

/* ---------- Cache fallback decision (Redis-down → DB) ---------- */

static void test_should_use_cache_only_on_ok(void) {
    /* Asserts that when the cache layer reports outage/timeout/miss/disabled
     * the handler falls back to the DB path (returns false here). Only a
     * positive hit (CACHE_OK) is served from cache. This is the unit-test
     * proxy for "Redis down → 200 via DB", not 500. */
    TEST_ASSERT_TRUE(should_use_cache(CACHE_OK));
    TEST_ASSERT_FALSE(should_use_cache(CACHE_MISS));
    TEST_ASSERT_FALSE(should_use_cache(CACHE_UNAVAILABLE));
    TEST_ASSERT_FALSE(should_use_cache(CACHE_DISABLED));
}

/* ---------- Cache key builders ---------- */

static void test_build_item_key(void) {
    char buf[32];
    int n = build_item_key(buf, sizeof(buf), 42);
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_STRING("data:42", buf);
}

static void test_build_all_key(void) {
    char buf[32];
    int n = build_all_key(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_STRING("data:all", buf);
}

/* ---------- check_api_key ---------- */

static void test_check_api_key_disabled_when_expected_empty(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_DISABLED, check_api_key(NULL, 0, NULL));
    TEST_ASSERT_EQUAL_INT(AUTH_DISABLED, check_api_key(NULL, 0, ""));
    /* Even a presented key is ignored when auth is disabled. */
    TEST_ASSERT_EQUAL_INT(AUTH_DISABLED, check_api_key("anything", 8, ""));
}

static void test_check_api_key_missing(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_MISSING, check_api_key(NULL, 0, "secret"));
    TEST_ASSERT_EQUAL_INT(AUTH_MISSING, check_api_key("any", 0, "secret"));
}

static void test_check_api_key_invalid_wrong_length(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_INVALID, check_api_key("secre", 5, "secret"));
    TEST_ASSERT_EQUAL_INT(AUTH_INVALID, check_api_key("secrets", 7, "secret"));
}

static void test_check_api_key_invalid_wrong_value(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_INVALID, check_api_key("nopeXX", 6, "secret"));
}

static void test_check_api_key_ok(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_OK, check_api_key("secret", 6, "secret"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_positive_long_accepts_simple);
    RUN_TEST(test_parse_positive_long_rejects_zero);
    RUN_TEST(test_parse_positive_long_rejects_negative);
    RUN_TEST(test_parse_positive_long_rejects_non_digit);
    RUN_TEST(test_parse_positive_long_rejects_empty);
    RUN_TEST(test_parse_positive_long_rejects_leading_zero);
    RUN_TEST(test_validate_content_nonempty);
    RUN_TEST(test_json_escape_passthrough);
    RUN_TEST(test_json_escape_quotes_and_backslash);
    RUN_TEST(test_json_escape_control_chars);
    RUN_TEST(test_json_escape_overflow);
    RUN_TEST(test_build_api_prefix);
    RUN_TEST(test_path_has_prefix);
    RUN_TEST(test_should_use_cache_only_on_ok);
    RUN_TEST(test_build_item_key);
    RUN_TEST(test_build_all_key);
    RUN_TEST(test_check_api_key_disabled_when_expected_empty);
    RUN_TEST(test_check_api_key_missing);
    RUN_TEST(test_check_api_key_invalid_wrong_length);
    RUN_TEST(test_check_api_key_invalid_wrong_value);
    RUN_TEST(test_check_api_key_ok);
    return UNITY_END();
}
