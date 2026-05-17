#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

#include "access_log.hpp"
#include "auth.hpp"
#include "cache.hpp"
#include "config.hpp"
#include "crow_all.h"
#include "db.hpp"
#include "handlers.hpp"

namespace {

// Per-request, per-thread user id stashed by extract_user() and read by the
// access-log middleware's after_handle. Crow handles each request on a single
// worker thread, so a thread_local is the cheapest path to thread the JWT sub
// through to logging without touching every handler signature.
thread_local std::string g_current_user_id = "-";

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
    {
        const std::string v = app::env_str("REDIS_TLS", "false");
        c.redis_tls = (v == "true" || v == "1");
    }
    c.cache_ttl_seconds = app::env_int("CACHE_TTL_SECONDS", 30);
    c.redis_timeout_ms = app::env_int("REDIS_TIMEOUT_MS", 200);
    c.max_body_bytes = static_cast<std::size_t>(app::env_int("MAX_BODY_BYTES", 1048576));
    c.api_key = app::env_str("API_KEY", "");
    c.api_key_next = app::env_str("API_KEY_NEXT", "");
    return c;
}

crow::response to_response(const app::HandlerResult &r) {
    crow::response resp(r.status, r.body);
    resp.set_header("Content-Type", "application/json");
    return resp;
}

crow::response auth_response(app::AuthStatus s) {
    if (s == app::AuthStatus::Missing) {
        crow::response r(401, R"({"error":"missing api key"})");
        r.set_header("Content-Type", "application/json");
        return r;
    }
    crow::response r(401, R"({"error":"invalid api key"})");
    r.set_header("Content-Type", "application/json");
    return r;
}

crow::response unauthorized() {
    crow::response r(401, R"({"error":"authentication required"})");
    r.set_header("Content-Type", "application/json");
    return r;
}

crow::response forbidden() {
    crow::response r(403, R"({"error":"forbidden"})");
    r.set_header("Content-Type", "application/json");
    return r;
}

// Access-log middleware. Records start time in before_handle, writes the
// full combined-format line in after_handle once res.code and res.body are
// known. Matches the JS (morgan) / Python (uvicorn-style) log format.
struct AccessLogMiddleware {
    struct context {
        std::chrono::steady_clock::time_point start;
    };
    app::AccessLog *log = nullptr; // not owned; set by main after construction

    void before_handle(crow::request &, crow::response &, context &ctx) {
        ctx.start = std::chrono::steady_clock::now();
        g_current_user_id = "-"; // reset per request
    }

    void after_handle(crow::request &req, crow::response &res, context &ctx) {
        if (!log)
            return;
        double elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ctx.start)
                .count();

        char ts[40];
        std::time_t now_t = std::time(nullptr);
        struct tm tm {};
        gmtime_r(&now_t, &tm);
        std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);

        std::string bytes = res.body.empty() ? "-" : std::to_string(res.body.size());

        char line[1024];
        std::snprintf(line, sizeof(line), "%s - %s [%s] \"%s %s HTTP/1.1\" %d %s - %.2f ms",
                      req.remote_ip_address.c_str(), g_current_user_id.c_str(), ts,
                      crow::method_name(req.method).c_str(), req.url.c_str(), res.code,
                      bytes.c_str(), elapsed_ms);
        log->write(line);
    }
};

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    app::Config cfg = load_config_from_env();
    const std::string jwt_secret = app::env_str("JWT_SECRET", "");
    const bool cookie_secure = app::env_str("COOKIE_SECURE", "true") != "false" &&
                               app::env_str("COOKIE_SECURE", "true") != "0";
    const std::string access_log_path = app::env_str("ACCESS_LOG_PATH", "./access.log");
    const std::size_t access_log_max_bytes =
        static_cast<std::size_t>(app::env_int("ACCESS_LOG_MAX_BYTES", 10 * 1024 * 1024));
    const int access_log_backups = app::env_int("ACCESS_LOG_BACKUPS", 5);

    if (cfg.api_key.empty())
        std::cerr << "[warn] API_KEY empty — API-key gate DISABLED\n";
    if (jwt_secret.empty())
        std::cerr << "[warn] JWT_SECRET empty — JWT verification DISABLED\n";

    auto db = std::make_unique<app::Database>(cfg.dsn());
    try {
        db->ensure_schema();
    } catch (const std::exception &e) {
        std::cerr << "[warn] could not ensure schema at startup: " << e.what() << "\n";
    }
    auto cache = std::make_unique<app::Cache>(cfg.redis_host, cfg.redis_port, cfg.redis_timeout_ms,
                                              cfg.cache_ttl_seconds, cfg.redis_tls);
    auto access_log =
        std::make_unique<app::AccessLog>(access_log_path, access_log_max_bytes, access_log_backups);

    app::AppDeps deps;
    deps.db = db.get();
    deps.cache = cache.get();
    deps.app_lang = cfg.app_lang;
    deps.cache_ttl_seconds = cfg.cache_ttl_seconds;
    deps.max_body_bytes = cfg.max_body_bytes;
    deps.api_key = cfg.api_key;
    deps.api_key_next = cfg.api_key_next;
    deps.jwt_secret = jwt_secret;
    if (!deps.api_key_next.empty())
        std::cerr << "[info] API_KEY_NEXT set — rotation in progress (both keys accepted)\n";
    deps.cookie_secure = cookie_secure;

    crow::App<AccessLogMiddleware> crow_app;
    crow_app.loglevel(crow::LogLevel::Warning);
    crow_app.get_middleware<AccessLogMiddleware>().log = access_log.get();

    auto api_key_check = [&deps](const crow::request &req) -> std::optional<crow::response> {
        auto auth = app::check_api_key_dual(req.get_header_value("X-API-Key"), deps.api_key,
                                            deps.api_key_next);
        if (auth == app::AuthStatus::Missing || auth == app::AuthStatus::Invalid) {
            return auth_response(auth);
        }
        return std::nullopt;
    };

    auto extract_user = [&deps](const crow::request &req) -> std::optional<app::CurrentUser> {
        if (deps.jwt_secret.empty())
            return std::nullopt;
        auto cookie_hdr = req.get_header_value("Cookie");
        auto token = app::cookie_get_session(cookie_hdr);
        if (!token)
            return std::nullopt;
        auto payload = app::jwt_verify_hs256(*token, deps.jwt_secret,
                                             static_cast<std::int64_t>(std::time(nullptr)));
        if (!payload)
            return std::nullopt;
        auto u = app::parse_user_payload(*payload);
        if (u.id > 0)
            g_current_user_id = std::to_string(u.id);
        return u;
    };

    // /health — public.
    CROW_ROUTE(crow_app, "/health").methods(crow::HTTPMethod::GET)([&deps]() {
        return to_response(app::handle_health(deps));
    });

    // /api/<lang>/auth/login
    CROW_ROUTE(crow_app, "/api/<string>/auth/login")
        .methods(crow::HTTPMethod::POST)(
            [&deps, api_key_check](const crow::request &req, std::string lang) {
                if (auto err = api_key_check(req))
                    return std::move(*err);
                if (lang != deps.app_lang) {
                    crow::response r(404, R"({"error":"not found"})");
                    r.set_header("Content-Type", "application/json");
                    return r;
                }
                auto lr = app::handle_login(deps, req.body);
                crow::response resp(lr.status, lr.body);
                resp.set_header("Content-Type", "application/json");
                if (!lr.set_cookie.empty())
                    resp.set_header("Set-Cookie", lr.set_cookie);
                return resp;
            });

    // /api/<lang>/auth/logout
    CROW_ROUTE(crow_app, "/api/<string>/auth/logout")
        .methods(crow::HTTPMethod::POST)(
            [&deps, api_key_check](const crow::request &req, std::string lang) {
                if (auto err = api_key_check(req))
                    return std::move(*err);
                if (lang != deps.app_lang) {
                    crow::response r(404, R"({"error":"not found"})");
                    r.set_header("Content-Type", "application/json");
                    return r;
                }
                std::string c = "session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
                if (deps.cookie_secure)
                    c += "; Secure";
                crow::response resp(204, "");
                resp.set_header("Set-Cookie", c);
                return resp;
            });

    // /api/<lang>/auth/me
    CROW_ROUTE(crow_app, "/api/<string>/auth/me")
        .methods(crow::HTTPMethod::GET)(
            [&deps, api_key_check, extract_user](const crow::request &req, std::string lang) {
                if (auto err = api_key_check(req))
                    return std::move(*err);
                if (lang != deps.app_lang) {
                    crow::response r(404, R"({"error":"not found"})");
                    r.set_header("Content-Type", "application/json");
                    return r;
                }
                auto u = extract_user(req);
                if (!u)
                    return unauthorized();
                return to_response(app::handle_me(*u));
            });

    // /api/<lang>/data (GET, POST)
    CROW_ROUTE(crow_app, "/api/<string>/data")
        .methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST)(
            [&deps, api_key_check, extract_user](const crow::request &req, std::string lang) {
                if (auto err = api_key_check(req))
                    return std::move(*err);
                if (lang != deps.app_lang) {
                    crow::response r(404, R"({"error":"not found"})");
                    r.set_header("Content-Type", "application/json");
                    return r;
                }
                auto user = extract_user(req);
                if (!deps.jwt_secret.empty() && !user)
                    return unauthorized();
                if (req.method == crow::HTTPMethod::POST) {
                    if (!deps.jwt_secret.empty() &&
                        !app::roles_contains_any(user->roles_json, {"writer", "admin"})) {
                        return forbidden();
                    }
                    return to_response(app::handle_create(deps, req.body));
                }
                return to_response(app::handle_list(deps));
            });

    // /api/<lang>/data/<id>
    CROW_ROUTE(crow_app, "/api/<string>/data/<string>")
        .methods(crow::HTTPMethod::GET)(
            [&deps, api_key_check, extract_user](const crow::request &req, std::string lang,
                                                 std::string id_str) {
                if (auto err = api_key_check(req))
                    return std::move(*err);
                if (lang != deps.app_lang) {
                    crow::response r(404, R"({"error":"not found"})");
                    r.set_header("Content-Type", "application/json");
                    return r;
                }
                if (!deps.jwt_secret.empty() && !extract_user(req))
                    return unauthorized();
                return to_response(app::handle_get_one(deps, id_str));
            });

    CROW_CATCHALL_ROUTE(crow_app)
    ([] {
        crow::response r(404, R"({"error":"not found"})");
        r.set_header("Content-Type", "application/json");
        return r;
    });

    std::cerr << "[info] " << cfg.app_lang << " api listening on 0.0.0.0:" << cfg.port
              << " prefix=" << cfg.api_prefix() << "\n";
    crow_app.port(cfg.port).bindaddr("0.0.0.0").multithreaded().run();
    return 0;
}
