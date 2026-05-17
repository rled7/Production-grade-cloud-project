#define _GNU_SOURCE
#include "auth.hpp"

#include <crypt.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <cctype>
#include <cstring>
#include <stdexcept>

namespace app {

namespace {

constexpr char B64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int decode_char(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

bool ct_eq(const unsigned char *a, const unsigned char *b, std::size_t n) {
    unsigned char d = 0;
    for (std::size_t i = 0; i < n; ++i)
        d |= static_cast<unsigned char>(a[i] ^ b[i]);
    return d == 0;
}

bool hmac_sha256(std::string_view secret, std::string_view msg, unsigned char out[32]) {
    unsigned int outlen = 32;
    return HMAC(EVP_sha256(), reinterpret_cast<const unsigned char *>(secret.data()),
                static_cast<int>(secret.size()),
                reinterpret_cast<const unsigned char *>(msg.data()), msg.size(), out,
                &outlen) != nullptr &&
           outlen == 32;
}

constexpr const char *HS256_HEADER_B64 = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";

std::int64_t parse_exp(const std::string &payload) {
    // Cheap scan for "exp":<digits>. Sufficient because we control encoding.
    auto p = payload.find("\"exp\"");
    if (p == std::string::npos)
        return 0;
    auto colon = payload.find(':', p);
    if (colon == std::string::npos)
        return 0;
    std::size_t i = colon + 1;
    while (i < payload.size() && std::isspace(static_cast<unsigned char>(payload[i])))
        ++i;
    std::int64_t v = 0;
    while (i < payload.size() && payload[i] >= '0' && payload[i] <= '9') {
        v = v * 10 + (payload[i] - '0');
        ++i;
    }
    return v;
}

} // namespace

std::string b64url_encode(const unsigned char *data, std::size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= len) {
        unsigned v = (static_cast<unsigned>(data[i]) << 16) |
                     (static_cast<unsigned>(data[i + 1]) << 8) | static_cast<unsigned>(data[i + 2]);
        out += B64URL[(v >> 18) & 0x3f];
        out += B64URL[(v >> 12) & 0x3f];
        out += B64URL[(v >> 6) & 0x3f];
        out += B64URL[v & 0x3f];
        i += 3;
    }
    if (i < len) {
        unsigned v = static_cast<unsigned>(data[i]) << 16;
        if (i + 1 < len)
            v |= static_cast<unsigned>(data[i + 1]) << 8;
        out += B64URL[(v >> 18) & 0x3f];
        out += B64URL[(v >> 12) & 0x3f];
        if (i + 1 < len)
            out += B64URL[(v >> 6) & 0x3f];
    }
    return out;
}

std::optional<std::vector<unsigned char>> b64url_decode(std::string_view in) {
    std::vector<unsigned char> out;
    out.reserve((in.size() * 3) / 4 + 4);
    int buf = 0, bits = 0;
    for (char c : in) {
        int v = decode_char(c);
        if (v < 0)
            return std::nullopt;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buf >> bits) & 0xff));
        }
    }
    return out;
}

std::string jwt_sign_hs256(std::string_view payload_json, std::string_view secret) {
    std::string payload_b64 = b64url_encode(
        reinterpret_cast<const unsigned char *>(payload_json.data()), payload_json.size());
    std::string msg = std::string(HS256_HEADER_B64) + "." + payload_b64;
    unsigned char sig[32];
    if (!hmac_sha256(secret, msg, sig))
        return "";
    std::string sig_b64 = b64url_encode(sig, 32);
    msg.push_back('.');
    msg += sig_b64;
    return msg;
}

std::optional<std::string> jwt_verify_hs256(std::string_view token, std::string_view secret,
                                            std::int64_t now_unix) {
    if (token.empty() || secret.empty())
        return std::nullopt;
    auto d1 = token.find('.');
    if (d1 == std::string_view::npos)
        return std::nullopt;
    auto d2 = token.find('.', d1 + 1);
    if (d2 == std::string_view::npos)
        return std::nullopt;

    std::string_view header_b64 = token.substr(0, d1);
    std::string_view payload_b64 = token.substr(d1 + 1, d2 - d1 - 1);
    std::string_view sig_b64 = token.substr(d2 + 1);

    if (header_b64 != HS256_HEADER_B64)
        return std::nullopt;

    std::string msg(token.substr(0, d2));
    unsigned char sig[32];
    if (!hmac_sha256(secret, msg, sig))
        return std::nullopt;
    std::string expected_sig = b64url_encode(sig, 32);
    if (expected_sig.size() != sig_b64.size())
        return std::nullopt;
    if (!ct_eq(reinterpret_cast<const unsigned char *>(expected_sig.data()),
               reinterpret_cast<const unsigned char *>(sig_b64.data()), sig_b64.size())) {
        return std::nullopt;
    }

    auto payload_bytes = b64url_decode(payload_b64);
    if (!payload_bytes)
        return std::nullopt;
    std::string payload(payload_bytes->begin(), payload_bytes->end());

    std::int64_t exp = parse_exp(payload);
    if (exp > 0 && now_unix >= exp)
        return std::nullopt;
    return payload;
}

std::optional<std::string> cookie_get_session(std::string_view hdr) {
    std::size_t i = 0;
    while (i < hdr.size()) {
        while (i < hdr.size() && (hdr[i] == ' ' || hdr[i] == ';'))
            ++i;
        if (i >= hdr.size())
            break;
        auto eq = hdr.find('=', i);
        if (eq == std::string_view::npos)
            return std::nullopt;
        auto val_start = eq + 1;
        auto val_end = hdr.find(';', val_start);
        if (val_end == std::string_view::npos)
            val_end = hdr.size();
        auto name = hdr.substr(i, eq - i);
        // case-insensitive compare for "session"
        std::string lower;
        lower.reserve(name.size());
        for (char c : name)
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        if (lower == "session") {
            return std::string(hdr.substr(val_start, val_end - val_start));
        }
        i = (val_end < hdr.size()) ? val_end + 1 : hdr.size();
    }
    return std::nullopt;
}

bool bcrypt_verify(const std::string &password, const std::string &stored_hash) {
    if (stored_hash.empty() || stored_hash[0] != '$')
        return false;
    struct crypt_data data {};
    const char *result = crypt_r(password.c_str(), stored_hash.c_str(), &data);
    if (!result)
        return false;
    std::size_t rlen = std::strlen(result);
    if (rlen != stored_hash.size())
        return false;
    return ct_eq(reinterpret_cast<const unsigned char *>(result),
                 reinterpret_cast<const unsigned char *>(stored_hash.data()), rlen);
}

bool roles_contains_any(std::string_view roles_json,
                        std::initializer_list<std::string_view> wanted) {
    std::size_t i = 0;
    while (i < roles_json.size()) {
        if (roles_json[i] == '"') {
            ++i;
            auto end = roles_json.find('"', i);
            if (end == std::string_view::npos)
                return false;
            std::string_view tok = roles_json.substr(i, end - i);
            for (auto w : wanted)
                if (tok == w)
                    return true;
            i = end + 1;
        } else {
            ++i;
        }
    }
    return false;
}

} // namespace app
