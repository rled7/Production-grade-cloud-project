#include "cache.h"

#include <hiredis/hiredis.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct cache_ctx {
    redisContext *rc;
    char host[256];
    int port;
    int timeout_ms;
};

bool should_use_cache(cache_status_t s) {
    return s == CACHE_OK;
}

int build_item_key(char *buf, size_t buflen, long id) {
    int n = snprintf(buf, buflen, "data:%ld", id);
    if (n < 0 || (size_t) n >= buflen) return -1;
    return n;
}

int build_all_key(char *buf, size_t buflen) {
    int n = snprintf(buf, buflen, "data:all");
    if (n < 0 || (size_t) n >= buflen) return -1;
    return n;
}

static struct timeval ms_to_tv(int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return tv;
}

static void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[cache][warn] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static bool try_connect(cache_ctx_t *ctx) {
    if (ctx->rc) {
        redisFree(ctx->rc);
        ctx->rc = NULL;
    }
    struct timeval tv = ms_to_tv(ctx->timeout_ms);
    redisContext *rc = redisConnectWithTimeout(ctx->host, ctx->port, tv);
    if (rc == NULL || rc->err) {
        if (rc) {
            log_warn("redis connect failed: %s", rc->errstr);
            redisFree(rc);
        } else {
            log_warn("redis connect failed: out of memory");
        }
        ctx->rc = NULL;
        return false;
    }
    if (redisSetTimeout(rc, tv) != REDIS_OK) {
        log_warn("redis set timeout failed");
        redisFree(rc);
        ctx->rc = NULL;
        return false;
    }
    ctx->rc = rc;
    return true;
}

cache_ctx_t *cache_connect(const char *host, int port, int timeout_ms) {
    if (host == NULL || host[0] == '\0') {
        return NULL;
    }
    cache_ctx_t *ctx = (cache_ctx_t *) calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    snprintf(ctx->host, sizeof(ctx->host), "%s", host);
    ctx->port = port;
    ctx->timeout_ms = timeout_ms > 0 ? timeout_ms : 200;
    (void) try_connect(ctx);
    /* Return ctx even if connection failed; we'll retry on each call. */
    return ctx;
}

void cache_free(cache_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->rc) redisFree(ctx->rc);
    free(ctx);
}

static bool ensure_conn(cache_ctx_t *ctx) {
    if (!ctx) return false;
    if (ctx->rc == NULL) return try_connect(ctx);
    return true;
}

cache_status_t cache_get(cache_ctx_t *ctx, const char *key, char **out) {
    if (out) *out = NULL;
    if (!ctx) return CACHE_DISABLED;
    if (!ensure_conn(ctx)) return CACHE_UNAVAILABLE;

    redisReply *r = (redisReply *) redisCommand(ctx->rc, "GET %s", key);
    if (r == NULL) {
        log_warn("GET %s failed: %s", key, ctx->rc->errstr);
        redisFree(ctx->rc);
        ctx->rc = NULL;
        return CACHE_UNAVAILABLE;
    }
    cache_status_t status;
    if (r->type == REDIS_REPLY_NIL) {
        status = CACHE_MISS;
    } else if (r->type == REDIS_REPLY_STRING) {
        if (out) {
            *out = (char *) malloc(r->len + 1);
            if (*out == NULL) {
                freeReplyObject(r);
                return CACHE_UNAVAILABLE;
            }
            memcpy(*out, r->str, r->len);
            (*out)[r->len] = '\0';
        }
        status = CACHE_OK;
    } else if (r->type == REDIS_REPLY_ERROR) {
        log_warn("GET %s error: %s", key, r->str);
        status = CACHE_UNAVAILABLE;
    } else {
        status = CACHE_MISS;
    }
    freeReplyObject(r);
    return status;
}

cache_status_t cache_set(cache_ctx_t *ctx, const char *key,
                         const char *value, int ttl_seconds) {
    if (!ctx) return CACHE_DISABLED;
    if (!ensure_conn(ctx)) return CACHE_UNAVAILABLE;
    if (ttl_seconds <= 0) ttl_seconds = 30;

    redisReply *r = (redisReply *) redisCommand(ctx->rc, "SETEX %s %d %s",
                                                key, ttl_seconds, value);
    if (r == NULL) {
        log_warn("SETEX %s failed: %s", key, ctx->rc->errstr);
        redisFree(ctx->rc);
        ctx->rc = NULL;
        return CACHE_UNAVAILABLE;
    }
    cache_status_t status = CACHE_OK;
    if (r->type == REDIS_REPLY_ERROR) {
        log_warn("SETEX %s error: %s", key, r->str);
        status = CACHE_UNAVAILABLE;
    }
    freeReplyObject(r);
    return status;
}

cache_status_t cache_del(cache_ctx_t *ctx, const char *key) {
    if (!ctx) return CACHE_DISABLED;
    if (!ensure_conn(ctx)) return CACHE_UNAVAILABLE;

    redisReply *r = (redisReply *) redisCommand(ctx->rc, "DEL %s", key);
    if (r == NULL) {
        log_warn("DEL %s failed: %s", key, ctx->rc->errstr);
        redisFree(ctx->rc);
        ctx->rc = NULL;
        return CACHE_UNAVAILABLE;
    }
    cache_status_t status = CACHE_OK;
    if (r->type == REDIS_REPLY_ERROR) {
        log_warn("DEL %s error: %s", key, r->str);
        status = CACHE_UNAVAILABLE;
    }
    freeReplyObject(r);
    return status;
}
