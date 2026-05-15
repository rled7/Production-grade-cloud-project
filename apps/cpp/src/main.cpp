#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "cache.hpp"
#include "config.hpp"
#include "crow_all.h"
#include "db.hpp"
#include "handlers.hpp"

namespace {

app::Config load_config_from_env() {
    app::Config c;
    c.port = app::env_int("PORT", 8080);
    c.app_lang = app::env_str("APP_LANG", "cpp");
    c.db_host = app::env_str("DB_HOST", "localhost");
    c.db_port = app::env_int("DB_PORT", 5432);
    c.db_name = app::env_str("DB_NAME", "appdb");
    c.db_user = app::env_str("DB_USER", "appuser");
    c.db_password = app::env_str("DB_PASSWORD", "");
    c.redis_host = app::env_str("REDIS_HOST", "localhost");
    c.redis_port = app::env_int("REDIS_PORT", 6379);
    c.cache_ttl_seconds = app::env_int("CACHE_TTL_SECONDS", 30);
    c.redis_timeout_ms = app::env_int("REDIS_TIMEOUT_MS", 200);
    c.max_body_bytes = static_cast<std::size_t>(app::env_int("MAX_BODY_BYTES", 1048576));
    return c;
}

crow::response to_response(const app::HandlerResult& r) {
    crow::response resp(r.status, r.body);
    resp.set_header("Content-Type", "application/json");
    return resp;
}

}  // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    app::Config cfg = load_config_from_env();

    // DB: best-effort schema bootstrap; survive failure (will retry on request).
    auto db = std::make_unique<app::Database>(cfg.dsn());
    try {
        db->ensure_schema();
    } catch (const std::exception& e) {
        std::cerr << "[warn] could not ensure schema at startup: " << e.what() << "\n";
    }

    // Cache: always constructed, internally degrades on connect/op failure.
    auto cache = std::make_unique<app::Cache>(cfg.redis_host, cfg.redis_port,
                                              cfg.redis_timeout_ms,
                                              cfg.cache_ttl_seconds);

    app::AppDeps deps;
    deps.db = db.get();
    deps.cache = cache.get();
    deps.app_lang = cfg.app_lang;
    deps.cache_ttl_seconds = cfg.cache_ttl_seconds;
    deps.max_body_bytes = cfg.max_body_bytes;

    crow::SimpleApp crow_app;
    crow_app.loglevel(crow::LogLevel::Warning);

    // Health check (used by ALB target group)
    CROW_ROUTE(crow_app, "/health").methods(crow::HTTPMethod::GET)
    ([&deps]() {
        return to_response(app::handle_health(deps));
    });

    // /api/<lang>/data — GET (list), POST (create)
    CROW_ROUTE(crow_app, "/api/<string>/data")
        .methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST)
    ([&deps](const crow::request& req, std::string lang) {
        if (lang != deps.app_lang) {
            crow::response r(404, R"({"error":"not found"})");
            r.set_header("Content-Type", "application/json");
            return r;
        }
        if (req.method == crow::HTTPMethod::GET) {
            return to_response(app::handle_list(deps));
        }
        return to_response(app::handle_create(deps, req.body));
    });

    // /api/<lang>/data/<id> — GET single. id captured as <string> so we can
    // return 400 "invalid id" for non-positive-int rather than a generic 404.
    CROW_ROUTE(crow_app, "/api/<string>/data/<string>")
        .methods(crow::HTTPMethod::GET)
    ([&deps](std::string lang, std::string id_str) {
        if (lang != deps.app_lang) {
            crow::response r(404, R"({"error":"not found"})");
            r.set_header("Content-Type", "application/json");
            return r;
        }
        return to_response(app::handle_get_one(deps, id_str));
    });

    CROW_CATCHALL_ROUTE(crow_app)([] {
        crow::response r(404, R"({"error":"not found"})");
        r.set_header("Content-Type", "application/json");
        return r;
    });

    std::cerr << "[info] " << cfg.app_lang << " api listening on 0.0.0.0:"
              << cfg.port << " prefix=" << cfg.api_prefix() << "\n";
    crow_app.port(cfg.port).bindaddr("0.0.0.0").multithreaded().run();
    return 0;
}
