#ifndef HANDLERS_H
#define HANDLERS_H

#include <stdbool.h>
#include <stddef.h>

#include "cache.h"
#include "db.h"
#include "vendor/mongoose.h"

typedef struct {
    db_ctx_t   *db;
    cache_ctx_t *cache;
    const char *app_lang;
    char        api_prefix[64];  /* "/api/<lang>" */
    size_t      api_prefix_len;
    int         cache_ttl_seconds;
    size_t      max_body_bytes;
} app_ctx_t;

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
