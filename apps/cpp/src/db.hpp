#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pqxx {
class connection;
}

namespace app {

struct DataItem {
    long long id;
    std::string content;
    // ISO-8601 UTC representation as returned by Postgres after our conversion.
    std::string created_at_iso;
};

class DatabaseUnavailable : public std::runtime_error {
   public:
    explicit DatabaseUnavailable(const std::string& msg)
        : std::runtime_error(msg) {}
};

class Database {
   public:
    explicit Database(std::string dsn);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Idempotent schema bootstrap. Throws DatabaseUnavailable on failure.
    void ensure_schema();

    std::vector<DataItem> list_all();
    std::optional<DataItem> get_by_id(long long id);
    DataItem insert(const std::string& content);

   private:
    pqxx::connection* ensure_conn();  // throws DatabaseUnavailable on failure
    void reset_conn();

    std::string dsn_;
    std::mutex mutex_;
    std::unique_ptr<pqxx::connection> conn_;
};

}  // namespace app
