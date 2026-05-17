#ifndef ACCESS_LOG_H
#define ACCESS_LOG_H

#include <stddef.h>

typedef struct access_log access_log_t;

/* Open a rotating access log writer.
 * path        : log file path (created if missing).
 * max_bytes   : rotate when current size exceeds this.
 * backups     : how many rotated copies to keep (.1 .. .N).
 * Returns NULL on failure (and the app should log without rotation). */
access_log_t *access_log_open(const char *path, size_t max_bytes, int backups);

/* Write one line. The implementation appends a newline if needed. */
void access_log_write(access_log_t *log, const char *line, size_t len);

void access_log_close(access_log_t *log);

#endif
