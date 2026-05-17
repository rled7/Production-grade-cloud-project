#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app {

// base64url helpers (no padding).
std::string b64url_encode(const unsigned char *data, std::size_t len);
std::optional<std::vector<unsigned char>> b64url_decode(std::string_view in);

// HS256 JWT.
std::string jwt_sign_hs256(std::string_view payload_json, std::string_view secret);
// Returns the payload JSON on success; nullopt on invalid signature / format /
// expired. Caller supplies the current unix time for exp comparison.
std::optional<std::string> jwt_verify_hs256(std::string_view token, std::string_view secret,
                                            std::int64_t now_unix);
// Dual-secret verify for graceful rotation. Tries `secret` first, then
// `secret_next` if non-empty. Returns the payload from the first match.
std::optional<std::string> jwt_verify_hs256_dual(std::string_view token,
                                                 std::string_view secret,
                                                 std::string_view secret_next,
                                                 std::int64_t now_unix);

// Parse `session=<token>` out of a Cookie header value.
std::optional<std::string> cookie_get_session(std::string_view cookie_header);

// bcrypt verify via libxcrypt's crypt_r. password and stored_hash must be
// NUL-terminated.
bool bcrypt_verify(const std::string &password, const std::string &stored_hash);

// True iff roles_json (a raw JSON array like ["writer","admin"]) contains any
// of the wanted role names.
bool roles_contains_any(std::string_view roles_json,
                        std::initializer_list<std::string_view> wanted);

} // namespace app
