#include <string.h>
#include <time.h>

#include "../src/auth.h"
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

/* ---------- base64url ---------- */

static void test_b64url_roundtrip(void) {
    const unsigned char input[] = "Hello, World!";
    char encoded[64];
    int n = b64url_encode(input, sizeof(input) - 1, encoded, sizeof(encoded));
    TEST_ASSERT_TRUE(n > 0);
    unsigned char decoded[64];
    int d = b64url_decode(encoded, n, decoded, sizeof(decoded));
    TEST_ASSERT_EQUAL_INT((int) sizeof(input) - 1, d);
    TEST_ASSERT_EQUAL_INT(0, memcmp(input, decoded, d));
}

/* ---------- jwt_sign_hs256 + verify roundtrip ---------- */

static void test_jwt_sign_verify_roundtrip(void) {
    const char *secret = "shhh-this-is-the-shared-secret";
    long long now = (long long) time(NULL);
    char payload[256];
    int pn = snprintf(payload, sizeof(payload),
                      "{\"sub\":\"42\",\"email\":\"a@b.c\",\"roles\":[\"writer\"],"
                      "\"iat\":%lld,\"exp\":%lld}", now, now + 60);
    char token[2048];
    int tn = jwt_sign_hs256(payload, pn, secret, strlen(secret),
                            token, sizeof(token));
    TEST_ASSERT_TRUE(tn > 0);

    char back[512];
    int r = jwt_verify_hs256(token, tn, secret, strlen(secret), now,
                             back, sizeof(back));
    TEST_ASSERT_EQUAL_INT(0, r);
    /* roundtripped payload must contain sub:"42" */
    TEST_ASSERT_NOT_NULL(strstr(back, "\"sub\":\"42\""));
}

static void test_jwt_verify_rejects_wrong_secret(void) {
    const char *secret = "good-secret";
    long long now = (long long) time(NULL);
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"sub\":\"1\",\"exp\":%lld}", now + 60);
    char token[2048];
    int tn = jwt_sign_hs256(payload, strlen(payload), secret, strlen(secret),
                            token, sizeof(token));
    TEST_ASSERT_TRUE(tn > 0);
    char back[256];
    int r = jwt_verify_hs256(token, tn, "bad-secret", 10, now, back, sizeof(back));
    TEST_ASSERT_NOT_EQUAL(0, r);
}

static void test_jwt_verify_rejects_expired(void) {
    const char *secret = "good-secret";
    long long now = (long long) time(NULL);
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"sub\":\"1\",\"exp\":%lld}", now - 10);
    char token[2048];
    int tn = jwt_sign_hs256(payload, strlen(payload), secret, strlen(secret),
                            token, sizeof(token));
    char back[256];
    int r = jwt_verify_hs256(token, tn, secret, strlen(secret), now,
                             back, sizeof(back));
    TEST_ASSERT_NOT_EQUAL(0, r);
}

/* ---------- cookie_get_session ---------- */

static void test_cookie_get_session_basic(void) {
    const char *hdr = "session=abc.def.ghi; other=foo";
    char out[64];
    int n = cookie_get_session(hdr, strlen(hdr), out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(11, n);
    TEST_ASSERT_EQUAL_STRING("abc.def.ghi", out);
}

static void test_cookie_get_session_other_first(void) {
    const char *hdr = "lang=en; session=zzz";
    char out[64];
    int n = cookie_get_session(hdr, strlen(hdr), out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_STRING("zzz", out);
}

static void test_cookie_get_session_missing(void) {
    const char *hdr = "lang=en; theme=dark";
    char out[64];
    TEST_ASSERT_EQUAL_INT(-1, cookie_get_session(hdr, strlen(hdr), out, sizeof(out)));
}

/* ---------- roles_contains_any ---------- */

static void test_roles_contains_any_hit(void) {
    const char *roles = "[\"reader\",\"writer\"]";
    const char *wanted[] = { "writer", "admin" };
    TEST_ASSERT_TRUE(roles_contains_any(roles, strlen(roles), wanted, 2));
}

static void test_roles_contains_any_miss(void) {
    const char *roles = "[\"reader\"]";
    const char *wanted[] = { "writer", "admin" };
    TEST_ASSERT_FALSE(roles_contains_any(roles, strlen(roles), wanted, 2));
}

static void test_roles_contains_any_empty(void) {
    const char *wanted[] = { "writer" };
    TEST_ASSERT_FALSE(roles_contains_any("[]", 2, wanted, 1));
}

/* ---------- Edge cases ---------- */

static void test_parse_positive_long_max(void) {
    long v = 0;
    TEST_ASSERT_TRUE(parse_positive_long("9223372036854775807", 19, &v));
    TEST_ASSERT_EQUAL_INT64(9223372036854775807LL, v);
    TEST_ASSERT_FALSE(parse_positive_long("99999999999999999999", 20, &v));
    TEST_ASSERT_FALSE(parse_positive_long("9223372036854775808", 19, &v));
}

static void test_json_escape_high_bytes_passthrough(void) {
    char buf[16];
    const unsigned char utf8[] = {0xc3, 0xa9, 0xc3, 0xb1, 0}; // é ñ
    int n = json_escape((const char *) utf8, 4, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_INT(0, memcmp(buf, utf8, 4));
}

static void test_json_escape_embedded_null(void) {
    char buf[16];
    char in[3] = { 'a', 0, 'b' };
    int n = json_escape(in, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(8, n);
    TEST_ASSERT_EQUAL_STRING("a\\u0000b", buf);
}

static void test_b64url_decode_rejects_invalid(void) {
    unsigned char out[16];
    TEST_ASSERT_EQUAL_INT(-1, b64url_decode("aaa!", 4, out, sizeof(out)));
    /* / and + are standard b64 chars but NOT url-safe; must be rejected. */
    TEST_ASSERT_EQUAL_INT(-1, b64url_decode("a/b+", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, b64url_decode("a b", 3, out, sizeof(out)));
}

static void test_cookie_get_session_truncated_buffer(void) {
    const char *hdr = "session=very-long-token-value";
    char out[8];
    TEST_ASSERT_EQUAL_INT(-1, cookie_get_session(hdr, strlen(hdr), out, sizeof(out)));
}

static void test_jwt_verify_malformed_tokens(void) {
    char back[256];
    const char *secret = "x";
    TEST_ASSERT_NOT_EQUAL(0, jwt_verify_hs256("onlyonedot", 10, secret, 1, 0, back, sizeof(back)));
    TEST_ASSERT_NOT_EQUAL(0, jwt_verify_hs256("a.b.c.d", 7, secret, 1, 0, back, sizeof(back)));
    TEST_ASSERT_NOT_EQUAL(0, jwt_verify_hs256("", 0, secret, 1, 0, back, sizeof(back)));
}

static void test_roles_contains_any_malformed(void) {
    const char *wanted[] = { "writer" };
    TEST_ASSERT_FALSE(roles_contains_any("not json", 8, wanted, 1));
    TEST_ASSERT_FALSE(roles_contains_any("[\"unterminated", 14, wanted, 1));
}

static void test_check_api_key_empty_presented_with_expected_set(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_MISSING, check_api_key(NULL, 0, "x"));
    TEST_ASSERT_EQUAL_INT(AUTH_MISSING, check_api_key("", 0, "x"));
}

static void test_check_api_key_dual_both_empty_disabled(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_DISABLED, check_api_key_dual(NULL, 0, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(AUTH_DISABLED, check_api_key_dual("x", 1, "", ""));
}

static void test_check_api_key_dual_primary_match(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_OK, check_api_key_dual("old", 3, "old", "new"));
}

static void test_check_api_key_dual_secondary_match(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_OK, check_api_key_dual("new", 3, "old", "new"));
}

static void test_check_api_key_dual_neither_matches(void) {
    TEST_ASSERT_EQUAL_INT(AUTH_INVALID, check_api_key_dual("nope", 4, "old", "new"));
}

static void test_check_api_key_dual_only_secondary_set(void) {
    /* primary off, secondary still enforces. */
    TEST_ASSERT_EQUAL_INT(AUTH_OK,      check_api_key_dual("new", 3, "", "new"));
    TEST_ASSERT_EQUAL_INT(AUTH_INVALID, check_api_key_dual("old", 3, "", "new"));
    TEST_ASSERT_EQUAL_INT(AUTH_MISSING, check_api_key_dual(NULL, 0, "", "new"));
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
    RUN_TEST(test_b64url_roundtrip);
    RUN_TEST(test_jwt_sign_verify_roundtrip);
    RUN_TEST(test_jwt_verify_rejects_wrong_secret);
    RUN_TEST(test_jwt_verify_rejects_expired);
    RUN_TEST(test_cookie_get_session_basic);
    RUN_TEST(test_cookie_get_session_other_first);
    RUN_TEST(test_cookie_get_session_missing);
    RUN_TEST(test_roles_contains_any_hit);
    RUN_TEST(test_roles_contains_any_miss);
    RUN_TEST(test_roles_contains_any_empty);
    RUN_TEST(test_parse_positive_long_max);
    RUN_TEST(test_json_escape_high_bytes_passthrough);
    RUN_TEST(test_json_escape_embedded_null);
    RUN_TEST(test_b64url_decode_rejects_invalid);
    RUN_TEST(test_cookie_get_session_truncated_buffer);
    RUN_TEST(test_jwt_verify_malformed_tokens);
    RUN_TEST(test_roles_contains_any_malformed);
    RUN_TEST(test_check_api_key_empty_presented_with_expected_set);
    RUN_TEST(test_check_api_key_dual_both_empty_disabled);
    RUN_TEST(test_check_api_key_dual_primary_match);
    RUN_TEST(test_check_api_key_dual_secondary_match);
    RUN_TEST(test_check_api_key_dual_neither_matches);
    RUN_TEST(test_check_api_key_dual_only_secondary_set);
    return UNITY_END();
}
