#ifndef HANDLERS_H
#define HANDLERS_H

#include <stdbool.h>
#include <stddef.h>

#include "cache.h"
#include "db.h"
#include "vendor/mongoose.h"

struct access_log;

typedef struct {
    db_ctx_t   *db;
    cache_ctx_t *cache;
    const char *app_lang;
    char        api_prefix[64];  /* "/api/<lang>" */
    size_t      api_prefix_len;
    int         cache_ttl_seconds;
    size_t      max_body_bytes;
    const char *api_key;         /* NULL/"" => API-key gate disabled */
    const char *api_key_next;    /* NULL/"" => no second key accepted (rotation off) */
    const char *jwt_secret;      /* NULL/"" => JWT verification disabled */
    bool        cookie_secure;   /* true: set Secure attribute on cookies */
    struct access_log *access_log;

    /* Per-request scratch — set by the reply helpers, read by ev_handler in
     * main.c after handle_request returns, then included in the access log
     * line. Mongoose is single-threaded so this is safe to share. */
    int    last_status;     /* HTTP status the request was answered with */
    size_t last_bytes;      /* body bytes written (0 => log "-") */
    long   last_user_id;    /* JWT sub if authenticated; -1 otherwise */
} app_ctx_t;

/* Result of an API-key check. AUTH_DISABLED means the server is configured
 * without an api_key and every request is allowed. */
typedef enum {
    AUTH_OK = 0,
    AUTH_MISSING = 1,
    AUTH_INVALID = 2,
    AUTH_DISABLED = 3,
} auth_status_t;

/* Pure auth check (unit-testable). `presented` may be NULL/0-length.
 * `expected` NULL/empty disables auth and returns AUTH_DISABLED. */
auth_status_t check_api_key(const char *presented, size_t presented_len,
                            const char *expected);

/* Same, but accepts EITHER `expected` or `expected_next` (rotation overlap).
 * AUTH_DISABLED only when BOTH are NULL/empty. */
auth_status_t check_api_key_dual(const char *presented, size_t presented_len,
                                 const char *expected,
                                 const char *expected_next);

/* ---------- Pure helpers (unit-testable, no I/O) ---------- */

/* Parse a positive integer (>= 1) from a string of given length.
 * Rejects empty, leading '+'/'-', non-digit chars, "0", and overflow. */
bool parse_positive_long(const char *s, size_t len, long *out);

/* Validate a content string for the POST body. Rejects NULL and zero-length. */
bool validate_content_nonempty(const char *s, size_t len);

/* JSON-escape `in` (in_len bytes) into `out` (capacity out_cap).
 * Does NOT add surrounding quotes. Writes a trailing NUL.
 * Returns bytes written excluding NUL, or -1 on overflow. */
int json_escape(const char *in, size_t in_len, char *out, size_t out_cap);

/* Build "/api/<lang>" into buf. Returns bytes written, or -1 on overflow. */
int build_api_prefix(const char *lang, char *buf, size_t buflen);

/* Check whether `path` (plen bytes) starts with `prefix` and the next
 * char is '/' or end-of-path. */
bool path_has_prefix(const char *path, size_t plen,
                     const char *prefix, size_t prefix_len);

/* ---------- Request dispatch ---------- */
void handle_request(struct mg_connection *c, struct mg_http_message *hm,
                    app_ctx_t *app);

#endif
