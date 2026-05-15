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
