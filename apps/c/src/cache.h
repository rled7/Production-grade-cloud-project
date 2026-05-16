#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Cache wrapper around hiredis with timeouts and graceful degradation.
 * All functions are designed to never crash the process on Redis failures.
 */

typedef enum {
    CACHE_OK = 0,           /* Operation succeeded, value available */
    CACHE_MISS = 1,         /* Key did not exist */
    CACHE_UNAVAILABLE = 2,  /* Redis down / timed out / error */
    CACHE_DISABLED = 3      /* Caller chose not to use cache (init failed) */
} cache_status_t;

typedef struct cache_ctx cache_ctx_t;

/* Decide whether a status counts as "served from cache". Pure function. */
bool should_use_cache(cache_status_t s);

/* Build a cache key for a single id, e.g. "data:42". Returns bytes written
 * (excluding NUL) or -1 on overflow. */
int build_item_key(char *buf, size_t buflen, long id);

/* Build the cache key for the "all items" list. Returns bytes written. */
int build_all_key(char *buf, size_t buflen);

/* Create/connect a cache context. Returns NULL on failure (caller should
 * continue without cache). host may be NULL/empty to disable.
 * When tls is true the connection is wrapped in TLS via hiredis_ssl. */
cache_ctx_t *cache_connect(const char *host, int port, int timeout_ms,
                           bool tls);

/* Free a cache context. */
void cache_free(cache_ctx_t *ctx);

/* GET key. On CACHE_OK, *out is malloc'd and must be free()'d by caller. */
cache_status_t cache_get(cache_ctx_t *ctx, const char *key, char **out);

/* SETEX key ttl value. */
cache_status_t cache_set(cache_ctx_t *ctx, const char *key,
                         const char *value, int ttl_seconds);

/* DEL key. */
cache_status_t cache_del(cache_ctx_t *ctx, const char *key);

#endif
