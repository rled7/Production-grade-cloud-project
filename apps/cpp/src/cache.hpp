#pragma once

#include <mutex>
#include <optional>
#include <string>

struct redisContext;
struct redisSSLContext;

namespace app {

enum class CacheStatus {
    Hit,
    Miss,
    Error,  // Redis unavailable/timeout/protocol error — caller falls back to DB.
};

struct CacheGetResult {
    CacheStatus status;
    std::string value;  // populated only when status == Hit
};

inline std::string cache_key_all() { return "data:all"; }
inline std::string cache_key_item(long long id) {
    return "data:" + std::to_string(id);
}

// Pure helper used by handlers and tested in unit tests:
// returns true if the cache lookup should be served as a cache hit,
// false (DB fallback) otherwise. Treats Error and Miss the same way for
// fall-through behaviour, but the caller distinguishes them for logging.
inline bool should_serve_from_cache(CacheStatus status) {
    return status == CacheStatus::Hit;
}

class Cache {
   public:
    Cache(std::string host, int port, int timeout_ms, int ttl_seconds,
          bool tls = false);
    ~Cache();

    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    // Returns Hit + value, Miss, or Error. Never throws.
    CacheGetResult get(const std::string& key);

    // Best-effort. Failures are logged and swallowed.
    void set(const std::string& key, const std::string& value);
    void del(const std::string& key);

   private:
    redisContext* ensure_ctx();   // returns nullptr on error
    void reset_ctx();

    std::string      host_;
    int              port_;
    int              timeout_ms_;
    int              ttl_seconds_;
    bool             tls_;
    redisSSLContext* ssl_ctx_ = nullptr;
    std::mutex       mutex_;
    redisContext*    ctx_ = nullptr;
};

}  // namespace app
