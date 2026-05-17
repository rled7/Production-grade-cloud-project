#include "access_log.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/stat.h>

namespace app {

AccessLog::AccessLog(std::string path, std::size_t max_bytes, int backups)
    : path_(std::move(path)), max_bytes_(max_bytes ? max_bytes : 10 * 1024 * 1024),
      backups_(backups > 0 ? backups : 5) {
    open_locked();
}

AccessLog::~AccessLog() {
    if (fp_)
        std::fclose(fp_);
}

void AccessLog::open_locked() {
    fp_ = std::fopen(path_.c_str(), "a");
    if (fp_)
        std::setvbuf(fp_, nullptr, _IOLBF, 0);
}

void AccessLog::rotate_locked() {
    if (fp_) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    char src[512], dst[512];
    for (int i = backups_; i >= 1; --i) {
        std::snprintf(src, sizeof(src), "%s.%d", path_.c_str(), i - 1);
        std::snprintf(dst, sizeof(dst), "%s.%d", path_.c_str(), i);
        std::rename(src, dst);
    }
    std::snprintf(dst, sizeof(dst), "%s.1", path_.c_str());
    std::rename(path_.c_str(), dst);
    open_locked();
}

void AccessLog::write(std::string_view line) {
    std::lock_guard<std::mutex> lk(mtx_);
    struct stat st;
    if (fp_ && ::stat(path_.c_str(), &st) == 0 &&
        static_cast<std::size_t>(st.st_size) >= max_bytes_) {
        rotate_locked();
    }
    if (fp_) {
        std::fwrite(line.data(), 1, line.size(), fp_);
        if (line.empty() || line.back() != '\n')
            std::fputc('\n', fp_);
    }
    // mirror to stderr for CloudWatch
    std::fwrite(line.data(), 1, line.size(), stderr);
    if (line.empty() || line.back() != '\n')
        std::fputc('\n', stderr);
}

} // namespace app
