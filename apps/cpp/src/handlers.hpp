#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cache.hpp"
#include "config.hpp"
#include "db.hpp"

namespace app {

// ---------- Pure helpers (no I/O) — unit-testable ----------

// Parse a positive integer (>= 1) from a string view.
// Rejects empty, leading '+'/'-' and leading zeros, non-digits, "0", overflow.
bool parse_positive_long(std::string_view s, long long& out);

// True if `s` is a non-empty string (after no further processing).
bool validate_content_nonempty(const std::string& s);

// JSON-escape `in` into a freshly allocated string. Does NOT add surrounding
// quotes. Encodes ", \, control chars, etc.
std::string json_escape(std::string_view in);

// Quote a string for JSON output (adds surrounding "" and escapes).
std::string json_quote(std::string_view in);

// Serialize a single DataItem as a JSON object.
std::string serialize_item(const DataItem& item);

// Serialize a list of DataItems as a JSON array.
std::string serialize_items(const std::vector<DataItem>& items);

// ---------- Handler entry points (used by main.cpp) ----------

struct AppDeps {
    Database* db = nullptr;       // not owned
    Cache* cache = nullptr;       // not owned (may be null if disabled)
    std::string app_lang;
    int cache_ttl_seconds = 30;
    std::size_t max_body_bytes = 1048576;
    std::string api_key;          // empty => API-key gate disabled
    std::string api_key_next;     // empty => no second key (rotation off)
    std::string jwt_secret;       // empty => JWT verification disabled
    bool cookie_secure = true;
};

// Decoded session claims (after JWT verify).
struct CurrentUser {
    long long   id = 0;
    std::string email;
    std::string roles_json;  // raw JSON array
};

// API-key check result. Disabled means the server is configured without a
// key and every request is allowed.
enum class AuthStatus { Ok, Missing, Invalid, Disabled };

// Pure auth check (unit-testable). presented may be empty.
AuthStatus check_api_key(const std::string& presented, const std::string& expected);

// Dual-key variant for graceful rotation. Returns Ok if presented matches
// either expected or expected_next. Disabled only when both are empty.
AuthStatus check_api_key_dual(const std::string& presented,
                              const std::string& expected,
                              const std::string& expected_next);

// Each returns (status_code, body_json). All callers wrap as crow::response.
struct HandlerResult {
    int status;
    std::string body;
};

HandlerResult handle_health(const AppDeps& deps);
HandlerResult handle_list(AppDeps& deps);
HandlerResult handle_get_one(AppDeps& deps, std::string_view id_str);
HandlerResult handle_create(AppDeps& deps, const std::string& body);

// Returns { 200, body, set-cookie } on success; { 401, body, "" } otherwise.
struct LoginResult {
    int         status = 401;
    std::string body;
    std::string set_cookie;
};
LoginResult handle_login(AppDeps& deps, const std::string& body);
HandlerResult handle_me(const CurrentUser& user);

// Parse "sub":<id>, "email":"...", "roles":[...] out of a JWT payload JSON.
CurrentUser parse_user_payload(const std::string& payload);

}  // namespace app
