#include "handlers.hpp"

#include <cctype>
#include <cstdio>
#include <iostream>
#include <limits>
#include <sstream>

#include "crow_all.h"

namespace app {

// ---------- Pure helpers ----------

bool parse_positive_long(std::string_view s, long long& out) {
    if (s.empty()) return false;
    if (s.size() > 1 && s[0] == '0') return false; // no leading zeros
    long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        int d = c - '0';
        if (v > (std::numeric_limits<long long>::max() - d) / 10) return false;
        v = v * 10 + d;
    }
    if (v <= 0) return false;
    out = v;
    return true;
}

bool validate_content_nonempty(const std::string& s) {
    return !s.empty();
}

std::string json_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string json_quote(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    out += '"';
    out += json_escape(in);
    out += '"';
    return out;
}

std::string serialize_item(const DataItem& item) {
    std::string out;
    out += "{\"id\":";
    out += std::to_string(item.id);
    out += ",\"content\":";
    out += json_quote(item.content);
    out += ",\"created_at\":";
    out += json_quote(item.created_at_iso);
    out += "}";
    return out;
}

std::string serialize_items(const std::vector<DataItem>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ',';
        out += serialize_item(items[i]);
    }
    out += "]";
    return out;
}

// ---------- Helpers for handlers ----------

namespace {

HandlerResult error_response(int code, std::string_view msg) {
    std::string body = "{\"error\":" + json_quote(msg) + "}";
    return {code, std::move(body)};
}

}  // namespace

// ---------- Handlers ----------

HandlerResult handle_health(const AppDeps& deps) {
    std::string body = "{\"status\":\"ok\",\"lang\":" + json_quote(deps.app_lang) + "}";
    return {200, std::move(body)};
}

HandlerResult handle_list(AppDeps& deps) {
    const std::string key = cache_key_all();

    if (deps.cache) {
        auto r = deps.cache->get(key);
        if (should_serve_from_cache(r.status)) {
            std::string body = "{\"source\":\"cache\",\"items\":" + r.value + "}";
            return {200, std::move(body)};
        }
        // Miss or Error → fall through to DB. Error is logged inside Cache.
    }

    try {
        auto rows = deps.db->list_all();
        std::string items_json = serialize_items(rows);
        if (deps.cache) deps.cache->set(key, items_json);
        std::string body = "{\"source\":\"db\",\"items\":" + items_json + "}";
        return {200, std::move(body)};
    } catch (const DatabaseUnavailable& e) {
        std::cerr << "[db] unavailable: " << e.what() << "\n";
        return error_response(503, "database unavailable");
    } catch (const std::exception& e) {
        std::cerr << "[db] error: " << e.what() << "\n";
        return error_response(500, "internal");
    }
}

HandlerResult handle_get_one(AppDeps& deps, std::string_view id_str) {
    long long id = 0;
    if (!parse_positive_long(id_str, id)) {
        return error_response(400, "invalid id");
    }

    const std::string key = cache_key_item(id);

    if (deps.cache) {
        auto r = deps.cache->get(key);
        if (should_serve_from_cache(r.status)) {
            std::string body = "{\"source\":\"cache\",\"item\":" + r.value + "}";
            return {200, std::move(body)};
        }
    }

    try {
        auto opt = deps.db->get_by_id(id);
        if (!opt) return error_response(404, "not found");
        std::string item_json = serialize_item(*opt);
        if (deps.cache) deps.cache->set(key, item_json);
        std::string body = "{\"source\":\"db\",\"item\":" + item_json + "}";
        return {200, std::move(body)};
    } catch (const DatabaseUnavailable& e) {
        std::cerr << "[db] unavailable: " << e.what() << "\n";
        return error_response(503, "database unavailable");
    } catch (const std::exception& e) {
        std::cerr << "[db] error: " << e.what() << "\n";
        return error_response(500, "internal");
    }
}

HandlerResult handle_create(AppDeps& deps, const std::string& body_str) {
    if (body_str.size() > deps.max_body_bytes) {
        return error_response(413, "payload too large");
    }

    auto json = crow::json::load(body_str);
    if (!json) {
        return error_response(400, "malformed json");
    }
    if (!json.has("content") || json["content"].t() != crow::json::type::String) {
        return error_response(400, "content is required and must be a non-empty string");
    }
    std::string content = json["content"].s();
    if (!validate_content_nonempty(content)) {
        return error_response(400, "content is required and must be a non-empty string");
    }

    try {
        auto item = deps.db->insert(content);
        if (deps.cache) deps.cache->del(cache_key_all());
        std::string body = "{\"item\":" + serialize_item(item) + "}";
        return {201, std::move(body)};
    } catch (const DatabaseUnavailable& e) {
        std::cerr << "[db] unavailable: " << e.what() << "\n";
        return error_response(503, "database unavailable");
    } catch (const std::exception& e) {
        std::cerr << "[db] error: " << e.what() << "\n";
        return error_response(500, "internal");
    }
}

}  // namespace app
