#include <gtest/gtest.h>

#include <string>

#include "cache.hpp"
#include "handlers.hpp"

using namespace app;

// ---------- parse_positive_long ----------

TEST(ParsePositiveLong, AcceptsSimple) {
    long long v = 0;
    EXPECT_TRUE(parse_positive_long("5", v));
    EXPECT_EQ(v, 5);

    EXPECT_TRUE(parse_positive_long("12345", v));
    EXPECT_EQ(v, 12345);
}

TEST(ParsePositiveLong, RejectsZero) {
    long long v = 0;
    EXPECT_FALSE(parse_positive_long("0", v));
}

TEST(ParsePositiveLong, RejectsNegative) {
    long long v = 0;
    EXPECT_FALSE(parse_positive_long("-1", v));
}

TEST(ParsePositiveLong, RejectsNonDigit) {
    long long v = 0;
    EXPECT_FALSE(parse_positive_long("abc", v));
    EXPECT_FALSE(parse_positive_long("12x", v));
    EXPECT_FALSE(parse_positive_long(" 5", v));
}

TEST(ParsePositiveLong, RejectsEmpty) {
    long long v = 0;
    EXPECT_FALSE(parse_positive_long("", v));
}

TEST(ParsePositiveLong, RejectsLeadingZero) {
    long long v = 0;
    EXPECT_FALSE(parse_positive_long("05", v));
}

// ---------- validate_content_nonempty ----------

TEST(ValidateContent, AcceptsNonEmpty) {
    EXPECT_TRUE(validate_content_nonempty("hi"));
}

TEST(ValidateContent, RejectsEmpty) {
    EXPECT_FALSE(validate_content_nonempty(""));
}

// ---------- json_escape / json_quote ----------

TEST(JsonEscape, Passthrough) {
    EXPECT_EQ(json_escape("hello"), "hello");
}

TEST(JsonEscape, QuotesAndBackslashes) {
    EXPECT_EQ(json_escape("a\"b\\c"), "a\\\"b\\\\c");
}

TEST(JsonEscape, ControlChars) {
    EXPECT_EQ(json_escape("a\nb\tc"), "a\\nb\\tc");
}

TEST(JsonQuote, Wraps) {
    EXPECT_EQ(json_quote("hi"), "\"hi\"");
    EXPECT_EQ(json_quote("a\"b"), "\"a\\\"b\"");
}

// ---------- serialize_item ----------

TEST(SerializeItem, Format) {
    DataItem it{42, "hello", "2024-05-14T10:00:00.000000Z"};
    EXPECT_EQ(serialize_item(it),
              R"({"id":42,"content":"hello","created_at":"2024-05-14T10:00:00.000000Z"})");
}

TEST(SerializeItems, EmptyArray) {
    std::vector<DataItem> items;
    EXPECT_EQ(serialize_items(items), "[]");
}

TEST(SerializeItems, MultipleItems) {
    std::vector<DataItem> items = {
        {1, "a", "2024-01-01T00:00:00.000000Z"},
        {2, "b", "2024-01-02T00:00:00.000000Z"},
    };
    EXPECT_EQ(serialize_items(items),
              R"([{"id":1,"content":"a","created_at":"2024-01-01T00:00:00.000000Z"},)"
              R"({"id":2,"content":"b","created_at":"2024-01-02T00:00:00.000000Z"}])");
}

// ---------- Cache fallback decision (Redis-down → DB) ----------

// Unit-test proxy for the "Redis timeout/outage returns 200 via DB fallback"
// guarantee: only a positive cache Hit serves from cache; Miss and Error both
// route the handler to the DB path. If this were not true, a Redis outage
// would propagate as a 5xx — which the contract forbids.
TEST(CacheFallback, OnlyHitServesFromCache) {
    EXPECT_TRUE(should_serve_from_cache(CacheStatus::Hit));
    EXPECT_FALSE(should_serve_from_cache(CacheStatus::Miss));
    EXPECT_FALSE(should_serve_from_cache(CacheStatus::Error));
}

// ---------- handle_health ----------

TEST(HandleHealth, ReturnsLang) {
    AppDeps deps;
    deps.app_lang = "cpp";
    auto r = handle_health(deps);
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.body.find("\"lang\":\"cpp\""), std::string::npos);
    EXPECT_NE(r.body.find("\"status\":\"ok\""), std::string::npos);
}

// ---------- check_api_key ----------

TEST(CheckApiKey, DisabledWhenExpectedEmpty) {
    EXPECT_EQ(check_api_key("", ""), AuthStatus::Disabled);
    EXPECT_EQ(check_api_key("anything", ""), AuthStatus::Disabled);
}

TEST(CheckApiKey, MissingWhenPresentedEmpty) {
    EXPECT_EQ(check_api_key("", "secret"), AuthStatus::Missing);
}

TEST(CheckApiKey, InvalidOnWrongLength) {
    EXPECT_EQ(check_api_key("secre", "secret"), AuthStatus::Invalid);
    EXPECT_EQ(check_api_key("secrets", "secret"), AuthStatus::Invalid);
}

TEST(CheckApiKey, InvalidOnWrongValue) {
    EXPECT_EQ(check_api_key("nopeXX", "secret"), AuthStatus::Invalid);
}

TEST(CheckApiKey, OkOnExactMatch) {
    EXPECT_EQ(check_api_key("secret", "secret"), AuthStatus::Ok);
}

// ---------- JWT / auth helpers ----------

#include <ctime>
#include "auth.hpp"

TEST(B64UrlRoundtrip, Works) {
    const char* in = "Hello, World!";
    auto enc = b64url_encode(reinterpret_cast<const unsigned char*>(in), 13);
    auto dec = b64url_decode(enc);
    ASSERT_TRUE(dec.has_value());
    std::string back(dec->begin(), dec->end());
    EXPECT_EQ(back, std::string(in));
}

TEST(JwtRoundtrip, VerifiesWithCorrectSecret) {
    const std::string secret = "shhh-shared-secret";
    long long now = std::time(nullptr);
    std::string payload = std::string("{\"sub\":\"42\",\"roles\":[\"writer\"],")
                          + "\"iat\":" + std::to_string(now)
                          + ",\"exp\":" + std::to_string(now + 60) + "}";
    auto token = jwt_sign_hs256(payload, secret);
    ASSERT_FALSE(token.empty());
    auto back = jwt_verify_hs256(token, secret, now);
    ASSERT_TRUE(back.has_value());
    EXPECT_NE(back->find("\"sub\":\"42\""), std::string::npos);
}

TEST(JwtVerify, RejectsWrongSecret) {
    long long now = std::time(nullptr);
    std::string payload = "{\"sub\":\"1\",\"exp\":" + std::to_string(now + 60) + "}";
    auto token = jwt_sign_hs256(payload, "good");
    EXPECT_FALSE(jwt_verify_hs256(token, "bad", now).has_value());
}

TEST(JwtVerify, RejectsExpired) {
    long long now = std::time(nullptr);
    std::string payload = "{\"sub\":\"1\",\"exp\":" + std::to_string(now - 10) + "}";
    auto token = jwt_sign_hs256(payload, "k");
    EXPECT_FALSE(jwt_verify_hs256(token, "k", now).has_value());
}

TEST(CookieGetSession, ExtractsValue) {
    auto t = cookie_get_session("session=abc.def.ghi; other=foo");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(*t, "abc.def.ghi");
}

TEST(CookieGetSession, FindsAfterOthers) {
    auto t = cookie_get_session("lang=en; session=zzz");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(*t, "zzz");
}

TEST(CookieGetSession, MissingReturnsNullopt) {
    EXPECT_FALSE(cookie_get_session("lang=en; theme=dark").has_value());
}

TEST(RolesContainsAny, HitsAndMisses) {
    EXPECT_TRUE(roles_contains_any("[\"reader\",\"writer\"]", {"writer", "admin"}));
    EXPECT_FALSE(roles_contains_any("[\"reader\"]", {"writer", "admin"}));
    EXPECT_FALSE(roles_contains_any("[]", {"writer"}));
}

TEST(ParseUserPayload, ExtractsClaims) {
    auto u = parse_user_payload(
        "{\"sub\":\"42\",\"email\":\"a@b.c\",\"roles\":[\"writer\",\"admin\"],"
        "\"iat\":1,\"exp\":2}");
    EXPECT_EQ(u.id, 42);
    EXPECT_EQ(u.email, "a@b.c");
    EXPECT_NE(u.roles_json.find("\"writer\""), std::string::npos);
    EXPECT_NE(u.roles_json.find("\"admin\""), std::string::npos);
}

TEST(HandleMe, ProducesJson) {
    CurrentUser u;
    u.id = 7; u.email = "x@y"; u.roles_json = "[\"reader\"]";
    auto r = handle_me(u);
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(r.body.find("\"id\":7"), std::string::npos);
    EXPECT_NE(r.body.find("\"roles\":[\"reader\"]"), std::string::npos);
}
