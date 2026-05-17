#define _GNU_SOURCE
#include "access_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct access_log {
    FILE *fp;
    char *path;
    size_t max_bytes;
    int backups;
};

static FILE *open_append(const char *path) {
    FILE *fp = fopen(path, "a");
    if (fp)
        setvbuf(fp, NULL, _IOLBF, 0);
    return fp;
}

access_log_t *access_log_open(const char *path, size_t max_bytes, int backups) {
    if (!path || !*path)
        return NULL;
    access_log_t *log = (access_log_t *)calloc(1, sizeof(*log));
    if (!log)
        return NULL;
    log->path = strdup(path);
    log->max_bytes = max_bytes ? max_bytes : (size_t)(10 * 1024 * 1024);
    log->backups = backups > 0 ? backups : 5;
    log->fp = open_append(path);
    if (!log->fp) {
        free(log->path);
        free(log);
        return NULL;
    }
    return log;
}

static void rotate_locked(access_log_t *log) {
    if (log->fp) {
        fclose(log->fp);
        log->fp = NULL;
    }
    /* shift .N-1 -> .N, ..., .1 -> .2, current -> .1 */
    char src[512], dst[512];
    for (int i = log->backups; i >= 1; i--) {
        snprintf(src, sizeof(src), "%s.%d", log->path, i - 1);
        snprintf(dst, sizeof(dst), "%s.%d", log->path, i);
        rename(src, dst); /* ignore failure (file may not exist) */
    }
    /* current → .1 */
    snprintf(dst, sizeof(dst), "%s.1", log->path);
    rename(log->path, dst);
    log->fp = open_append(log->path);
}

void access_log_write(access_log_t *log, const char *line, size_t len) {
    if (!log) {
        /* fall back to stderr */
        fwrite(line, 1, len, stderr);
        if (len == 0 || line[len - 1] != '\n')
            fputc('\n', stderr);
        return;
    }
    /* check size, rotate if needed */
    struct stat st;
    if (log->fp && stat(log->path, &st) == 0 && (size_t)st.st_size >= log->max_bytes) {
        rotate_locked(log);
    }
    if (log->fp) {
        fwrite(line, 1, len, log->fp);
        if (len == 0 || line[len - 1] != '\n')
            fputc('\n', log->fp);
    }
    /* mirror to stderr for CloudWatch */
    fwrite(line, 1, len, stderr);
    if (len == 0 || line[len - 1] != '\n')
        fputc('\n', stderr);
}

void access_log_close(access_log_t *log) {
    if (!log)
        return;
    if (log->fp)
        fclose(log->fp);
    free(log->path);
    free(log);
}
