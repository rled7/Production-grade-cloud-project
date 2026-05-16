#define _GNU_SOURCE
#include "auth.h"

#include <crypt.h>
#include <ctype.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------- base64url ---------- */

static const char B64URL_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int b64url_encode(const unsigned char *in, size_t in_len, char *out, size_t out_cap) {
    size_t needed = ((in_len + 2) / 3) * 4;
    /* drop padding chars */
    size_t pad = (3 - (in_len % 3)) % 3;
    needed -= pad;
    if (needed + 1 > out_cap) return -1;

    size_t i = 0, o = 0;
    while (i + 3 <= in_len) {
        unsigned v = ((unsigned) in[i] << 16) | ((unsigned) in[i + 1] << 8) | (unsigned) in[i + 2];
        out[o++] = B64URL_ALPHABET[(v >> 18) & 0x3f];
        out[o++] = B64URL_ALPHABET[(v >> 12) & 0x3f];
        out[o++] = B64URL_ALPHABET[(v >> 6)  & 0x3f];
        out[o++] = B64URL_ALPHABET[v & 0x3f];
        i += 3;
    }
    if (i < in_len) {
        unsigned v = (unsigned) in[i] << 16;
        if (i + 1 < in_len) v |= (unsigned) in[i + 1] << 8;
        out[o++] = B64URL_ALPHABET[(v >> 18) & 0x3f];
        out[o++] = B64URL_ALPHABET[(v >> 12) & 0x3f];
        if (i + 1 < in_len) out[o++] = B64URL_ALPHABET[(v >> 6) & 0x3f];
    }
    out[o] = '\0';
    return (int) o;
}

static int b64url_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

int b64url_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap) {
    size_t o = 0;
    int buf = 0, bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        int v = b64url_decode_char(in[i]);
        if (v < 0) return -1;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= out_cap) return -1;
            out[o++] = (unsigned char) ((buf >> bits) & 0xff);
        }
    }
    return (int) o;
}

/* ---------- HS256 sign / verify ---------- */

static int hmac_sha256(const char *secret, size_t secret_len,
                       const char *msg, size_t msg_len,
                       unsigned char out[32]) {
    unsigned int outlen = 32;
    unsigned char *r = HMAC(EVP_sha256(),
                            (const unsigned char *) secret, (int) secret_len,
                            (const unsigned char *) msg, msg_len,
                            out, &outlen);
    return (r != NULL && outlen == 32) ? 0 : -1;
}

static const char HS256_HEADER_JSON[] = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
static const char HS256_HEADER_B64[] = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9";
static const size_t HS256_HEADER_B64_LEN = sizeof(HS256_HEADER_B64) - 1;

int jwt_sign_hs256(const char *payload_json, size_t payload_len,
                   const char *secret, size_t secret_len,
                   char *out, size_t out_cap) {
    if (!payload_json || !secret || !out) return -1;

    /* payload base64url */
    size_t payload_b64_cap = ((payload_len + 2) / 3) * 4 + 4;
    char *payload_b64 = (char *) malloc(payload_b64_cap);
    if (!payload_b64) return -1;
    int plen = b64url_encode((const unsigned char *) payload_json, payload_len,
                             payload_b64, payload_b64_cap);
    if (plen < 0) { free(payload_b64); return -1; }

    /* message = header_b64 + "." + payload_b64 */
    size_t msg_cap = HS256_HEADER_B64_LEN + 1 + (size_t) plen + 1;
    char *msg = (char *) malloc(msg_cap);
    if (!msg) { free(payload_b64); return -1; }
    int msg_len = snprintf(msg, msg_cap, "%s.%s", HS256_HEADER_B64, payload_b64);
    if (msg_len < 0 || (size_t) msg_len >= msg_cap) { free(payload_b64); free(msg); return -1; }

    /* signature = HMAC-SHA256(secret, message) -> base64url */
    unsigned char sig[32];
    if (hmac_sha256(secret, secret_len, msg, (size_t) msg_len, sig) != 0) {
        free(payload_b64); free(msg); return -1;
    }
    char sig_b64[64];
    int siglen = b64url_encode(sig, 32, sig_b64, sizeof(sig_b64));
    if (siglen < 0) { free(payload_b64); free(msg); return -1; }

    int written = snprintf(out, out_cap, "%s.%s", msg, sig_b64);
    free(payload_b64);
    free(msg);
    if (written < 0 || (size_t) written >= out_cap) return -1;
    return written;
}

static int ct_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *) a;
    const unsigned char *y = (const unsigned char *) b;
    unsigned char d = 0;
    for (size_t i = 0; i < n; i++) d |= (unsigned char) (x[i] ^ y[i]);
    return d;
}

int jwt_verify_hs256(const char *token, size_t token_len,
                     const char *secret, size_t secret_len,
                     long long current_time_unix,
                     char *payload_out, size_t payload_cap) {
    if (!token || token_len == 0) return -1;

    /* find the two dots */
    const char *dot1 = memchr(token, '.', token_len);
    if (!dot1) return -1;
    size_t off1 = (size_t) (dot1 - token);
    const char *dot2 = memchr(dot1 + 1, '.', token_len - off1 - 1);
    if (!dot2) return -1;
    size_t off2 = (size_t) (dot2 - token);

    const char *header_b64  = token;
    size_t header_len       = off1;
    const char *payload_b64 = token + off1 + 1;
    size_t payload_b64_len  = off2 - off1 - 1;
    const char *sig_b64     = token + off2 + 1;
    size_t sig_b64_len      = token_len - off2 - 1;

    /* header must be exactly HS256 */
    if (header_len != HS256_HEADER_B64_LEN ||
        memcmp(header_b64, HS256_HEADER_B64, HS256_HEADER_B64_LEN) != 0) {
        return -1;
    }

    /* compute expected HMAC of "<header>.<payload>" */
    size_t msg_len = header_len + 1 + payload_b64_len;
    unsigned char sig[32];
    if (hmac_sha256(secret, secret_len, token, msg_len, sig) != 0) return -1;
    char expected_sig_b64[64];
    int exp_len = b64url_encode(sig, 32, expected_sig_b64, sizeof(expected_sig_b64));
    if (exp_len < 0) return -1;
    if ((size_t) exp_len != sig_b64_len) return -1;
    if (ct_memcmp(expected_sig_b64, sig_b64, sig_b64_len) != 0) return -1;

    /* decode payload */
    int decoded = b64url_decode(payload_b64, payload_b64_len,
                                (unsigned char *) payload_out, payload_cap - 1);
    if (decoded < 0) return -1;
    payload_out[decoded] = '\0';

    /* check exp claim (very small JSON parser: find "exp":N) */
    const char *exp_key = strstr(payload_out, "\"exp\"");
    if (exp_key) {
        const char *colon = strchr(exp_key, ':');
        if (colon) {
            long long exp_val = strtoll(colon + 1, NULL, 10);
            if (exp_val > 0 && current_time_unix >= exp_val) return -1;
        }
    }
    return 0;
}

/* ---------- Cookie parsing ---------- */

int cookie_get_session(const char *cookie_header, size_t cookie_len,
                       char *token_out, size_t token_cap) {
    if (!cookie_header || cookie_len == 0) return -1;
    const char *p = cookie_header;
    const char *end = cookie_header + cookie_len;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == ';')) p++;
        if (p >= end) break;
        const char *eq = memchr(p, '=', (size_t) (end - p));
        if (!eq) return -1;
        size_t name_len = (size_t) (eq - p);
        const char *val = eq + 1;
        const char *semi = memchr(val, ';', (size_t) (end - val));
        size_t val_len = semi ? (size_t) (semi - val) : (size_t) (end - val);
        if (name_len == 7 && strncasecmp(p, "session", 7) == 0) {
            if (val_len + 1 > token_cap) return -1;
            memcpy(token_out, val, val_len);
            token_out[val_len] = '\0';
            return (int) val_len;
        }
        p = semi ? semi + 1 : end;
    }
    return -1;
}

/* ---------- bcrypt verify (libxcrypt's crypt_r) ---------- */

bool bcrypt_verify(const char *password, const char *stored_hash) {
    if (!password || !stored_hash) return false;
    if (stored_hash[0] != '$') return false;
    struct crypt_data data;
    memset(&data, 0, sizeof(data));
    char *out = crypt_r(password, stored_hash, &data);
    if (!out) return false;
    /* Compare in constant-ish time. The output is the same length as the input
     * hash on success. */
    size_t a = strlen(out);
    size_t b = strlen(stored_hash);
    if (a != b) return false;
    return ct_memcmp(out, stored_hash, a) == 0;
}

/* ---------- roles_contains_any ----------
 * Accepts the raw substring of a JSON array, e.g. '["writer","admin"]'.
 * Linear scan with string compare; small N is fine. */

static bool slice_eq(const char *s, size_t n, const char *cstr) {
    size_t cl = strlen(cstr);
    return cl == n && memcmp(s, cstr, n) == 0;
}

bool roles_contains_any(const char *roles_json, size_t roles_len,
                        const char **wanted, size_t wanted_count) {
    if (!roles_json || roles_len < 2) return false;
    const char *p = roles_json;
    const char *end = roles_json + roles_len;
    while (p < end) {
        if (*p == '"') {
            p++;
            const char *q = memchr(p, '"', (size_t) (end - p));
            if (!q) return false;
            for (size_t i = 0; i < wanted_count; i++) {
                if (slice_eq(p, (size_t) (q - p), wanted[i])) return true;
            }
            p = q + 1;
        } else {
            p++;
        }
    }
    return false;
}
