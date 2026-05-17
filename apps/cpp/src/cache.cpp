#include "cache.hpp"

#include <hiredis/hiredis.h>
#include <hiredis/hiredis_ssl.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <utility>

namespace app {

namespace {

void log_warn(const std::string &msg) {
    std::cerr << "[cache] WARN " << msg << std::endl;
}

std::once_flag g_ssl_init_once;
void init_openssl_once() {
    std::call_once(g_ssl_init_once, []() { redisInitOpenSSL(); });
}

} // namespace

Cache::Cache(std::string host, int port, int timeout_ms, int ttl_seconds, bool tls)
    : host_(std::move(host)), port_(port), timeout_ms_(timeout_ms), ttl_seconds_(ttl_seconds),
      tls_(tls) {
    if (tls_) {
        init_openssl_once();
        redisSSLContextError err = REDIS_SSL_CTX_NONE;
        ssl_ctx_ = redisCreateSSLContext(nullptr, nullptr, nullptr, nullptr, host_.c_str(), &err);
        if (!ssl_ctx_) {
            log_warn("redisCreateSSLContext failed");
        }
    }
}

Cache::~Cache() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
    if (ssl_ctx_) {
        redisFreeSSLContext(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

void Cache::reset_ctx() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

redisContext *Cache::ensure_ctx() {
    if (ctx_ != nullptr && ctx_->err == 0) {
        return ctx_;
    }
    reset_ctx();

    struct timeval tv;
    tv.tv_sec = timeout_ms_ / 1000;
    tv.tv_usec = (timeout_ms_ % 1000) * 1000;

    redisContext *c = redisConnectWithTimeout(host_.c_str(), port_, tv);
    if (c == nullptr) {
        log_warn("redisConnectWithTimeout returned NULL");
        return nullptr;
    }
    if (c->err) {
        log_warn(std::string("redis connect error: ") + c->errstr);
        redisFree(c);
        return nullptr;
    }
    if (redisSetTimeout(c, tv) != REDIS_OK) {
        log_warn("redisSetTimeout failed");
        redisFree(c);
        return nullptr;
    }
    if (tls_ && ssl_ctx_) {
        if (redisInitiateSSLWithContext(c, ssl_ctx_) != REDIS_OK) {
            log_warn(std::string("redis TLS handshake failed: ") + c->errstr);
            redisFree(c);
            return nullptr;
        }
    }
    ctx_ = c;
    return ctx_;
}

CacheGetResult Cache::get(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    CacheGetResult res{CacheStatus::Error, {}};
    try {
        redisContext *c = ensure_ctx();
        if (c == nullptr) {
            return res;
        }
        redisReply *reply =
            static_cast<redisReply *>(redisCommand(c, "GET %b", key.data(), key.size()));
        if (reply == nullptr) {
            log_warn(std::string("GET reply NULL: ") + (c->errstr[0] ? c->errstr : "unknown"));
            reset_ctx();
            return res;
        }
        if (reply->type == REDIS_REPLY_NIL) {
            freeReplyObject(reply);
            res.status = CacheStatus::Miss;
            return res;
        }
        if (reply->type == REDIS_REPLY_STRING) {
            res.status = CacheStatus::Hit;
            res.value.assign(reply->str, reply->len);
            freeReplyObject(reply);
            return res;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            log_warn(std::string("GET error: ") + (reply->str ? reply->str : ""));
            freeReplyObject(reply);
            return res;
        }
        // Unexpected type
        freeReplyObject(reply);
        return res;
    } catch (const std::exception &e) {
        log_warn(std::string("get exception: ") + e.what());
        reset_ctx();
        return res;
    } catch (...) {
        log_warn("get unknown exception");
        reset_ctx();
        return res;
    }
}

void Cache::set(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        redisContext *c = ensure_ctx();
        if (c == nullptr)
            return;
        redisReply *reply =
            static_cast<redisReply *>(redisCommand(c, "SET %b %b EX %d", key.data(), key.size(),
                                                   value.data(), value.size(), ttl_seconds_));
        if (reply == nullptr) {
            log_warn(std::string("SET reply NULL: ") + (c->errstr[0] ? c->errstr : "unknown"));
            reset_ctx();
            return;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            log_warn(std::string("SET error: ") + (reply->str ? reply->str : ""));
        }
        freeReplyObject(reply);
    } catch (const std::exception &e) {
        log_warn(std::string("set exception: ") + e.what());
        reset_ctx();
    } catch (...) {
        log_warn("set unknown exception");
        reset_ctx();
    }
}

void Cache::del(const std::string &key) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        redisContext *c = ensure_ctx();
        if (c == nullptr)
            return;
        redisReply *reply =
            static_cast<redisReply *>(redisCommand(c, "DEL %b", key.data(), key.size()));
        if (reply == nullptr) {
            log_warn(std::string("DEL reply NULL: ") + (c->errstr[0] ? c->errstr : "unknown"));
            reset_ctx();
            return;
        }
        if (reply->type == REDIS_REPLY_ERROR) {
            log_warn(std::string("DEL error: ") + (reply->str ? reply->str : ""));
        }
        freeReplyObject(reply);
    } catch (const std::exception &e) {
        log_warn(std::string("del exception: ") + e.what());
        reset_ctx();
    } catch (...) {
        log_warn("del unknown exception");
        reset_ctx();
    }
}

} // namespace app
