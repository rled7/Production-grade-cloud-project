#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "db.h"
#include "handlers.h"
#include "vendor/mongoose.h"

static volatile sig_atomic_t s_stop = 0;

static void on_signal(int sig) {
    (void) sig;
    s_stop = 1;
}

static const char *env_or(const char *k, const char *d) {
    const char *v = getenv(k);
    return (v && *v) ? v : d;
}

static int env_int(const char *k, int d) {
    const char *v = getenv(k);
    return (v && *v) ? atoi(v) : d;
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev != MG_EV_HTTP_MSG) return;
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    app_ctx_t *app = (app_ctx_t *) c->fn_data;
    handle_request(c, hm, app);
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int port = env_int("PORT", 8080);
    const char *app_lang = env_or("APP_LANG", "c");
    const char *db_host = env_or("DB_HOST", "localhost");
    const char *db_port = env_or("DB_PORT", "5432");
    const char *db_name = env_or("DB_NAME", "appdb");
    const char *db_user = env_or("DB_USER", "appuser");
    const char *db_pass = env_or("DB_PASSWORD", "");
    const char *redis_host = env_or("REDIS_HOST", "localhost");
    int redis_port = env_int("REDIS_PORT", 6379);
    int ttl = env_int("CACHE_TTL_SECONDS", 30);
    int redis_timeout = env_int("REDIS_TIMEOUT_MS", 200);
    size_t max_body = (size_t) env_int("MAX_BODY_BYTES", 1048576);
    const char *api_key = env_or("API_KEY", "");

    app_ctx_t app_ctx;
    memset(&app_ctx, 0, sizeof(app_ctx));
    app_ctx.app_lang = app_lang;
    app_ctx.cache_ttl_seconds = ttl;
    app_ctx.max_body_bytes = max_body;
    app_ctx.api_key = api_key;
    if (!api_key[0]) {
        fprintf(stderr, "[warn] API_KEY env var is empty — auth is DISABLED.\n");
    }
    int n = build_api_prefix(app_lang, app_ctx.api_prefix,
                             sizeof(app_ctx.api_prefix));
    if (n < 0) {
        fprintf(stderr, "[fatal] APP_LANG too long\n");
        return 1;
    }
    app_ctx.api_prefix_len = (size_t) n;

    app_ctx.db = db_connect(db_host, db_port, db_name, db_user, db_pass);
    if (app_ctx.db && db_ensure_schema(app_ctx.db) != DB_OK) {
        fprintf(stderr, "[warn] could not ensure schema on startup; will retry on demand\n");
    }
    app_ctx.cache = cache_connect(redis_host, redis_port, redis_timeout);
    if (!app_ctx.cache) {
        fprintf(stderr, "[warn] redis unavailable at startup; serving directly from DB\n");
    }

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    char url[64];
    snprintf(url, sizeof(url), "http://0.0.0.0:%d", port);
    if (mg_http_listen(&mgr, url, ev_handler, &app_ctx) == NULL) {
        fprintf(stderr, "[fatal] failed to bind %s\n", url);
        return 1;
    }
    fprintf(stderr, "[info] %s api listening on %s prefix=%s\n",
            app_lang, url, app_ctx.api_prefix);

    while (!s_stop) mg_mgr_poll(&mgr, 1000);

    fprintf(stderr, "[info] shutting down\n");
    mg_mgr_free(&mgr);
    cache_free(app_ctx.cache);
    db_free(app_ctx.db);
    return 0;
}
