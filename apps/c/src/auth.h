#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stddef.h>

/* base64url encode/decode (no padding). Return bytes written, or -1 on overflow. */
int b64url_encode(const unsigned char *in, size_t in_len, char *out, size_t out_cap);
int b64url_decode(const char *in, size_t in_len, unsigned char *out, size_t out_cap);

/* Sign a JSON payload as an HS256 JWT. Writes "<header>.<payload>.<sig>" into out.
 * Returns bytes written excluding NUL, or -1 on overflow / failure. */
int jwt_sign_hs256(const char *payload_json, size_t payload_len, const char *secret,
                   size_t secret_len, char *out, size_t out_cap);

/* Verify an HS256 JWT and copy its payload JSON into payload_out.
 * Returns 0 on success, -1 on any failure (bad format, bad signature, expired).
 * Caller passes current_time_unix for exp comparison. */
int jwt_verify_hs256(const char *token, size_t token_len, const char *secret, size_t secret_len,
                     long long current_time_unix, char *payload_out, size_t payload_cap);

/* Dual-secret variant for graceful rotation. Tries `secret` first, then
 * `secret_next` if non-NULL/non-empty. Returns 0 on first successful verify. */
int jwt_verify_hs256_dual(const char *token, size_t token_len,
                          const char *secret, size_t secret_len,
                          const char *secret_next, size_t secret_next_len,
                          long long current_time_unix,
                          char *payload_out, size_t payload_cap);

/* Extract `session=<token>` from a Cookie header value. Returns token length
 * written into token_out (excl NUL), or -1 if cookie not present / too large. */
int cookie_get_session(const char *cookie_header, size_t cookie_len, char *token_out,
                       size_t token_cap);

/* bcrypt verify using libxcrypt's crypt_r. Returns true if `password` matches
 * the stored bcrypt hash (e.g. "$2b$12$..."). Constant-time on the final
 * compare; bcrypt itself is constant-time. */
bool bcrypt_verify(const char *password, const char *stored_hash);

/* Check whether the roles JSON array (as a substring of a payload JSON, OR a
 * parsed list) contains any of the given role names. */
bool roles_contains_any(const char *roles_csv, size_t roles_csv_len, const char **wanted,
                        size_t wanted_count);

#endif
