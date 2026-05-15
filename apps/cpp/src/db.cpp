#include "db.hpp"

#include <pqxx/pqxx>
#include <iostream>
#include <utility>

namespace app {

namespace {

constexpr const char* SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS data ("
    "id SERIAL PRIMARY KEY, "
    "content TEXT NOT NULL, "
    "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()"
    ");";

// Cast created_at to ISO-8601 UTC via Postgres for portability across pqxx
// versions and timezone settings.
constexpr const char* SELECT_ALL_SQL =
    "SELECT id, content, "
    "to_char(created_at AT TIME ZONE 'UTC', "
    "'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"') "
    "FROM data ORDER BY created_at DESC";

constexpr const char* SELECT_BY_ID_SQL =
    "SELECT id, content, "
    "to_char(created_at AT TIME ZONE 'UTC', "
    "'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"') "
    "FROM data WHERE id = $1";

constexpr const char* INSERT_SQL =
    "INSERT INTO data (content) VALUES ($1) "
    "RETURNING id, content, "
    "to_char(created_at AT TIME ZONE 'UTC', "
    "'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"')";

}  // namespace

Database::Database(std::string dsn) : dsn_(std::move(dsn)) {}

Database::~Database() = default;

pqxx::connection* Database::ensure_conn() {
    if (conn_ && conn_->is_open()) {
        return conn_.get();
    }
    try {
        conn_ = std::make_unique<pqxx::connection>(dsn_);
        if (!conn_->is_open()) {
            conn_.reset();
            throw DatabaseUnavailable("postgres connection not open");
        }
        return conn_.get();
    } catch (const pqxx::broken_connection& e) {
        conn_.reset();
        throw DatabaseUnavailable(std::string("connect failed: ") + e.what());
    } catch (const std::exception& e) {
        conn_.reset();
        throw DatabaseUnavailable(std::string("connect failed: ") + e.what());
    }
}

void Database::reset_conn() {
    conn_.reset();
}

void Database::ensure_schema() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto* c = ensure_conn();
        pqxx::work tx(*c);
        tx.exec(SCHEMA_SQL);
        tx.commit();
    } catch (const pqxx::broken_connection& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("ensure_schema: ") + e.what());
    } catch (const DatabaseUnavailable&) {
        throw;
    } catch (const std::exception& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("ensure_schema: ") + e.what());
    }
}

std::vector<DataItem> Database::list_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto* c = ensure_conn();
        pqxx::work tx(*c);
        auto result = tx.exec(SELECT_ALL_SQL);
        tx.commit();
        std::vector<DataItem> out;
        out.reserve(result.size());
        for (const auto& row : result) {
            DataItem item;
            item.id = row[0].as<long long>();
            item.content = row[1].as<std::string>();
            item.created_at_iso = row[2].as<std::string>();
            out.push_back(std::move(item));
        }
        return out;
    } catch (const pqxx::broken_connection& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("list_all: ") + e.what());
    } catch (const DatabaseUnavailable&) {
        throw;
    } catch (const std::exception& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("list_all: ") + e.what());
    }
}

std::optional<DataItem> Database::get_by_id(long long id) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto* c = ensure_conn();
        pqxx::work tx(*c);
        auto result = tx.exec_params(SELECT_BY_ID_SQL, id);
        tx.commit();
        if (result.empty()) return std::nullopt;
        const auto& row = result[0];
        DataItem item;
        item.id = row[0].as<long long>();
        item.content = row[1].as<std::string>();
        item.created_at_iso = row[2].as<std::string>();
        return item;
    } catch (const pqxx::broken_connection& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("get_by_id: ") + e.what());
    } catch (const DatabaseUnavailable&) {
        throw;
    } catch (const std::exception& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("get_by_id: ") + e.what());
    }
}

DataItem Database::insert(const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        auto* c = ensure_conn();
        pqxx::work tx(*c);
        auto result = tx.exec_params(INSERT_SQL, content);
        tx.commit();
        if (result.empty()) {
            throw DatabaseUnavailable("insert returned no rows");
        }
        const auto& row = result[0];
        DataItem item;
        item.id = row[0].as<long long>();
        item.content = row[1].as<std::string>();
        item.created_at_iso = row[2].as<std::string>();
        return item;
    } catch (const pqxx::broken_connection& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("insert: ") + e.what());
    } catch (const DatabaseUnavailable&) {
        throw;
    } catch (const std::exception& e) {
        reset_conn();
        throw DatabaseUnavailable(std::string("insert: ") + e.what());
    }
}

}  // namespace app
