#include "handlers.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Pure helpers ---------- */

bool parse_positive_long(const char *s, size_t len, long *out) {
    if (s == NULL || len == 0) return false;
    if (s[0] == '0' && len > 1) return false; /* no leading zeros */
    long v = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c < '0' || c > '9') return false;
        int d = c - '0';
        if (v > (LONG_MAX - d) / 10) return false; /* overflow */
        v = v * 10 + d;
    }
    if (v <= 0) return false;
    if (out) *out = v;
    return true;
}

bool validate_content_nonempty(const char *s, size_t len) {
    return s != NULL && len > 0;
}

int json_escape(const char *in, size_t in_len, char *out, size_t out_cap) {
    if (out == NULL || out_cap == 0) return -1;
    size_t w = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char) in[i];
        const char *esc = NULL;
        char buf[8];
        size_t need = 0;
        switch (c) {
            case '"':  esc = "\\\""; need = 2; break;
            case '\\': esc = "\\\\"; need = 2; break;
            case '\b': esc = "\\b";  need = 2; break;
            case '\f': esc = "\\f";  need = 2; break;
            case '\n': esc = "\\n";  need = 2; break;
            case '\r': esc = "\\r";  need = 2; break;
            case '\t': esc = "\\t";  need = 2; break;
            default:
                if (c < 0x20) {
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    esc = buf;
                    need = 6;
                } else {
                    if (w + 1 >= out_cap) return -1;
                    out[w++] = (char) c;
                    continue;
                }
        }
        if (w + need >= out_cap) return -1;
        memcpy(out + w, esc, need);
        w += need;
    }
    if (w >= out_cap) return -1;
    out[w] = '\0';
    return (int) w;
}

int build_api_prefix(const char *lang, char *buf, size_t buflen) {
    int n = snprintf(buf, buflen, "/api/%s", lang ? lang : "");
    if (n < 0 || (size_t) n >= buflen) return -1;
    return n;
}

bool path_has_prefix(const char *path, size_t plen,
                     const char *prefix, size_t prefix_len) {
    if (plen < prefix_len) return false;
    if (memcmp(path, prefix, prefix_len) != 0) return false;
    if (plen == prefix_len) return true;
    return path[prefix_len] == '/';
}

auth_status_t check_api_key(const char *presented, size_t presented_len,
                            const char *expected) {
    if (expected == NULL || expected[0] == '\0') return AUTH_DISABLED;
    if (presented == NULL || presented_len == 0) return AUTH_MISSING;
    size_t exp_len = strlen(expected);
    if (presented_len != exp_len) return AUTH_INVALID;
    /* constant-time compare to avoid timing oracles. */
    unsigned char diff = 0;
    for (size_t i = 0; i < exp_len; i++) {
        diff |= (unsigned char) presented[i] ^ (unsigned char) expected[i];
    }
    return diff == 0 ? AUTH_OK : AUTH_INVALID;
}

/* ---------- Strbuf (dynamic buffer) ---------- */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
    bool   oom;
} sb_t;

static void sb_init(sb_t *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    sb->oom = false;
}

static void sb_free(sb_t *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static bool sb_reserve(sb_t *sb, size_t extra) {
    if (sb->oom) return false;
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) return true;
    size_t cap = sb->cap == 0 ? 256 : sb->cap;
    while (cap < need) cap *= 2;
    char *p = realloc(sb->data, cap);
    if (!p) { sb->oom = true; return false; }
    sb->data = p;
    sb->cap = cap;
    return true;
}

static void sb_append(sb_t *sb, const char *s, size_t n) {
    if (!sb_reserve(sb, n)) return;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void sb_append_cstr(sb_t *sb, const char *s) {
    sb_append(sb, s, strlen(s));
}

static void sb_append_quoted(sb_t *sb, const char *s, size_t n) {
    sb_append(sb, "\"", 1);
    /* worst case: every byte expands to 6 (\uXXXX). */
    if (!sb_reserve(sb, n * 6 + 1)) return;
    int w = json_escape(s, n, sb->data + sb->len, sb->cap - sb->len);
    if (w < 0) { sb->oom = true; return; }
    sb->len += (size_t) w;
    sb_append(sb, "\"", 1);
}

static void sb_append_long(sb_t *sb, long v) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%ld", v);
    if (n > 0) sb_append(sb, buf, (size_t) n);
}

/* ---------- JSON serializers ---------- */

static void serialize_row(const db_row_t *row, sb_t *sb) {
    sb_append_cstr(sb, "{\"id\":");
    sb_append_long(sb, row->id);
    sb_append_cstr(sb, ",\"content\":");
    sb_append_quoted(sb, row->content ? row->content : "",
                     row->content ? strlen(row->content) : 0);
    sb_append_cstr(sb, ",\"created_at\":");
    sb_append_quoted(sb, row->created_at ? row->created_at : "",
                     row->created_at ? strlen(row->created_at) : 0);
    sb_append_cstr(sb, "}");
}

static void serialize_rows_array(const db_rows_t *rows, sb_t *sb) {
    sb_append_cstr(sb, "[");
    for (size_t i = 0; i < rows->count; i++) {
        if (i > 0) sb_append_cstr(sb, ",");
        serialize_row(&rows->rows[i], sb);
    }
    sb_append_cstr(sb, "]");
}

/* ---------- Response helpers ---------- */

static void reply_json(struct mg_connection *c, int code, const char *body,
                       size_t body_len) {
    mg_http_reply(c, code, "Content-Type: application/json\r\n",
                  "%.*s", (int) body_len, body);
}

static void reply_error(struct mg_connection *c, int code, const char *msg) {
    sb_t sb;
    sb_init(&sb);
    sb_append_cstr(&sb, "{\"error\":");
    sb_append_quoted(&sb, msg, strlen(msg));
    sb_append_cstr(&sb, "}");
    if (!sb.oom && sb.data) {
        reply_json(c, code, sb.data, sb.len);
    } else {
        mg_http_reply(c, code, "Content-Type: application/json\r\n",
                      "{\"error\":\"internal\"}");
    }
    sb_free(&sb);
}

/* ---------- Endpoint impls ---------- */

static void handle_health(struct mg_connection *c, app_ctx_t *app) {
    mg_http_reply(c, 200, "Content-Type: application/json\r\n",
                  "{\"status\":\"ok\",\"lang\":\"%s\"}", app->app_lang);
}

static void handle_list(struct mg_connection *c, app_ctx_t *app) {
    char key[32];
    build_all_key(key, sizeof(key));

    /* Cache lookup (graceful degradation on error). */
    char *cached = NULL;
    cache_status_t cs = cache_get(app->cache, key, &cached);
    if (cs == CACHE_OK && cached) {
        sb_t sb;
        sb_init(&sb);
        sb_append_cstr(&sb, "{\"source\":\"cache\",\"items\":");
        sb_append_cstr(&sb, cached);
        sb_append_cstr(&sb, "}");
        free(cached);
        if (!sb.oom) reply_json(c, 200, sb.data, sb.len);
        else reply_error(c, 500, "internal");
        sb_free(&sb);
        return;
    }
    free(cached);

    db_rows_t rows = (db_rows_t){0};
    db_status_t st = db_query_all(app->db, &rows);
    if (st == DB_UNAVAILABLE) {
        reply_error(c, 503, "database unavailable");
        return;
    }
    if (st != DB_OK) {
        reply_error(c, 500, "internal");
        return;
    }

    sb_t inner;
    sb_init(&inner);
    serialize_rows_array(&rows, &inner);
    db_rows_free(&rows);

    if (inner.oom || !inner.data) {
        sb_free(&inner);
        reply_error(c, 500, "internal");
        return;
    }

    /* Best-effort cache write (ignore failures). */
    cache_set(app->cache, key, inner.data, app->cache_ttl_seconds);

    sb_t resp;
    sb_init(&resp);
    sb_append_cstr(&resp, "{\"source\":\"db\",\"items\":");
    sb_append(&resp, inner.data, inner.len);
    sb_append_cstr(&resp, "}");
    sb_free(&inner);

    if (!resp.oom) reply_json(c, 200, resp.data, resp.len);
    else reply_error(c, 500, "internal");
    sb_free(&resp);
}

static void handle_one(struct mg_connection *c, app_ctx_t *app,
                       const char *id_str, size_t id_len) {
    long id;
    if (!parse_positive_long(id_str, id_len, &id)) {
        reply_error(c, 400, "invalid id");
        return;
    }

    char key[32];
    build_item_key(key, sizeof(key), id);

    char *cached = NULL;
    cache_status_t cs = cache_get(app->cache, key, &cached);
    if (cs == CACHE_OK && cached) {
        sb_t sb;
        sb_init(&sb);
        sb_append_cstr(&sb, "{\"source\":\"cache\",\"item\":");
        sb_append_cstr(&sb, cached);
        sb_append_cstr(&sb, "}");
        free(cached);
        if (!sb.oom) reply_json(c, 200, sb.data, sb.len);
        else reply_error(c, 500, "internal");
        sb_free(&sb);
        return;
    }
    free(cached);

    db_row_t row = (db_row_t){0};
    db_status_t st = db_query_one(app->db, id, &row);
    if (st == DB_NOT_FOUND) {
        reply_error(c, 404, "not found");
        return;
    }
    if (st == DB_UNAVAILABLE) {
        reply_error(c, 503, "database unavailable");
        return;
    }
    if (st != DB_OK) {
        reply_error(c, 500, "internal");
        return;
    }

    sb_t inner;
    sb_init(&inner);
    serialize_row(&row, &inner);
    db_row_free(&row);

    if (inner.oom || !inner.data) {
        sb_free(&inner);
        reply_error(c, 500, "internal");
        return;
    }

    cache_set(app->cache, key, inner.data, app->cache_ttl_seconds);

    sb_t resp;
    sb_init(&resp);
    sb_append_cstr(&resp, "{\"source\":\"db\",\"item\":");
    sb_append(&resp, inner.data, inner.len);
    sb_append_cstr(&resp, "}");
    sb_free(&inner);

    if (!resp.oom) reply_json(c, 200, resp.data, resp.len);
    else reply_error(c, 500, "internal");
    sb_free(&resp);
}

static void handle_create(struct mg_connection *c, app_ctx_t *app,
                          struct mg_http_message *hm) {
    if (hm->body.len > app->max_body_bytes) {
        reply_error(c, 413, "payload too large");
        return;
    }
    /* Detect malformed JSON: try to find a value at root. */
    if (mg_json_get(hm->body, "$", NULL) < 0) {
        reply_error(c, 400, "malformed json");
        return;
    }
    char *content = mg_json_get_str(hm->body, "$.content");
    if (!content || !validate_content_nonempty(content, strlen(content))) {
        free(content);
        reply_error(c, 400, "content is required and must be a non-empty string");
        return;
    }

    db_row_t row = (db_row_t){0};
    db_status_t st = db_insert(app->db, content, &row);
    free(content);

    if (st == DB_UNAVAILABLE) {
        reply_error(c, 503, "database unavailable");
        return;
    }
    if (st != DB_OK) {
        reply_error(c, 500, "internal");
        return;
    }

    /* Invalidate list cache (best-effort). */
    char key[32];
    build_all_key(key, sizeof(key));
    cache_del(app->cache, key);

    sb_t inner;
    sb_init(&inner);
    serialize_row(&row, &inner);
    db_row_free(&row);

    if (inner.oom || !inner.data) {
        sb_free(&inner);
        reply_error(c, 500, "internal");
        return;
    }

    sb_t resp;
    sb_init(&resp);
    sb_append_cstr(&resp, "{\"item\":");
    sb_append(&resp, inner.data, inner.len);
    sb_append_cstr(&resp, "}");
    sb_free(&inner);

    if (!resp.oom) reply_json(c, 201, resp.data, resp.len);
    else reply_error(c, 500, "internal");
    sb_free(&resp);
}

/* ---------- Dispatch ---------- */

void handle_request(struct mg_connection *c, struct mg_http_message *hm,
                    app_ctx_t *app) {
    const char *path = hm->uri.buf;
    size_t plen = hm->uri.len;
    const char *meth = hm->method.buf;
    size_t mlen = hm->method.len;

    /* /health (GET) — always public for ALB target group health checks. */
    if (plen == 7 && memcmp(path, "/health", 7) == 0) {
        if (mlen == 3 && memcmp(meth, "GET", 3) == 0) {
            handle_health(c, app);
        } else {
            reply_error(c, 405, "method not allowed");
        }
        return;
    }

    /* API-key auth on every non-/health route. */
    {
        struct mg_str *h = mg_http_get_header(hm, "X-API-Key");
        const char *pres = h ? h->buf : NULL;
        size_t pres_len = h ? h->len : 0;
        switch (check_api_key(pres, pres_len, app->api_key)) {
            case AUTH_OK:
            case AUTH_DISABLED:
                break;
            case AUTH_MISSING:
                reply_error(c, 401, "missing api key");
                return;
            case AUTH_INVALID:
                reply_error(c, 401, "invalid api key");
                return;
        }
    }

    /* /api/<lang>/data and /api/<lang>/data/<id> */
    if (!path_has_prefix(path, plen, app->api_prefix, app->api_prefix_len)) {
        reply_error(c, 404, "not found");
        return;
    }

    /* sub-path after prefix, e.g. "/data" or "/data/42" */
    const char *sub = path + app->api_prefix_len;
    size_t sublen = plen - app->api_prefix_len;

    bool is_data = (sublen == 5 && memcmp(sub, "/data", 5) == 0);
    bool is_data_id = (sublen > 6 && memcmp(sub, "/data/", 6) == 0);

    if (is_data) {
        if (mlen == 3 && memcmp(meth, "GET", 3) == 0) {
            handle_list(c, app);
        } else if (mlen == 4 && memcmp(meth, "POST", 4) == 0) {
            handle_create(c, app, hm);
        } else {
            reply_error(c, 405, "method not allowed");
        }
        return;
    }

    if (is_data_id) {
        if (!(mlen == 3 && memcmp(meth, "GET", 3) == 0)) {
            reply_error(c, 405, "method not allowed");
            return;
        }
        const char *id_str = sub + 6;
        size_t id_len = sublen - 6;
        handle_one(c, app, id_str, id_len);
        return;
    }

    reply_error(c, 404, "not found");
}
