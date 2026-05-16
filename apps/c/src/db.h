#ifndef DB_H
#define DB_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Database wrapper around libpq with reconnect logic.
 */

typedef struct db_ctx db_ctx_t;

typedef struct {
    long id;
    char *content;     /* malloc'd, must be free()'d via db_row_free */
    char *created_at;  /* ISO-8601 string, malloc'd */
} db_row_t;

typedef struct {
    db_row_t *rows;
    size_t count;
} db_rows_t;

typedef enum {
    DB_OK = 0,
    DB_NOT_FOUND = 1,
    DB_UNAVAILABLE = 2,
    DB_BAD_INPUT = 3,
    DB_INTERNAL = 4
} db_status_t;

/* Build a connection string and connect. Returns NULL on hard failure
 * (e.g. OOM); a returned ctx may still be in a disconnected state and will
 * reconnect lazily. */
db_ctx_t *db_connect(const char *host, const char *port, const char *dbname,
                     const char *user, const char *password);

void db_free(db_ctx_t *ctx);

/* Ensures CONNECTION_OK, reconnecting if necessary. */
bool db_ensure(db_ctx_t *ctx);

/* CREATE TABLE IF NOT EXISTS data ... — idempotent. */
db_status_t db_ensure_schema(db_ctx_t *ctx);

/* SELECT all rows ordered by created_at DESC. */
db_status_t db_query_all(db_ctx_t *ctx, db_rows_t *out);

/* SELECT one row by id. */
db_status_t db_query_one(db_ctx_t *ctx, long id, db_row_t *out);

/* INSERT a row, returning the new row. content must be NUL-terminated. */
db_status_t db_insert(db_ctx_t *ctx, const char *content, db_row_t *out);

typedef struct {
    long  id;
    char *email;
    char *password_hash;
    char *roles_json;   /* JSON array literal, e.g. ["writer","admin"] */
} db_user_t;

/* SELECT id, email, password_hash, roles FROM users WHERE LOWER(email)=LOWER($1).
 * On DB_OK fills out; on DB_NOT_FOUND nothing to free. */
db_status_t db_find_user_by_email(db_ctx_t *ctx, const char *email, db_user_t *out);

void db_user_free(db_user_t *user);

void db_row_free(db_row_t *row);
void db_rows_free(db_rows_t *rows);

#endif
