#pragma once

#include <cstddef>
#include <cstdio>
#include <mutex>
#include <string>

namespace app {

class AccessLog {
  public:
    AccessLog(std::string path, std::size_t max_bytes, int backups);
    ~AccessLog();

    AccessLog(const AccessLog &) = delete;
    AccessLog &operator=(const AccessLog &) = delete;

    void write(std::string_view line);

  private:
    void rotate_locked();
    void open_locked();

    std::string path_;
    std::size_t max_bytes_;
    int backups_;
    std::FILE *fp_ = nullptr;
    std::mutex mtx_;
};

} // namespace app
