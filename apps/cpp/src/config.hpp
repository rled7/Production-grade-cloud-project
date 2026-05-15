#pragma once

#include <cstdlib>
#include <string>

namespace app {

struct Config {
    int port = 8080;
    std::string app_lang = "cpp";
    std::string db_host = "localhost";
    int db_port = 5432;
    std::string db_name = "appdb";
    std::string db_user = "app";
    std::string db_password = "";
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int cache_ttl_seconds = 30;
    int redis_timeout_ms = 200;
    std::size_t max_body_bytes = 1048576;
    std::string api_key = "";  // empty => auth disabled

    std::string api_prefix() const { return "/api/" + app_lang; }
    std::string dsn() const {
        return "host=" + db_host + " port=" + std::to_string(db_port) +
               " dbname=" + db_name + " user=" + db_user +
               " password=" + db_password;
    }
};

inline std::string env_str(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return def;
    return std::string(v);
}

inline int env_int(const char* name, int def) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

inline std::size_t env_size(const char* name, std::size_t def) {
    const char* v = std::getenv(name);
    if (v == nullptr || *v == '\0') return def;
    try {
        long long parsed = std::stoll(v);
        if (parsed < 0) return def;
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        return def;
    }
}

inline Config load_config() {
    Config c;
    c.port = env_int("PORT", 8080);
    c.app_lang = env_str("APP_LANG", "cpp");
    c.db_host = env_str("DB_HOST", "localhost");
    c.db_port = env_int("DB_PORT", 5432);
    c.db_name = env_str("DB_NAME", "appdb");
    c.db_user = env_str("DB_USER", "app");
    c.db_password = env_str("DB_PASSWORD", "");
    c.redis_host = env_str("REDIS_HOST", "localhost");
    c.redis_port = env_int("REDIS_PORT", 6379);
    c.cache_ttl_seconds = env_int("CACHE_TTL_SECONDS", 30);
    c.redis_timeout_ms = env_int("REDIS_TIMEOUT_MS", 200);
    c.max_body_bytes = env_size("MAX_BODY_BYTES", 1048576);
    return c;
}

}  // namespace app
