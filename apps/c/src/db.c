#include "db.h"

#include <libpq-fe.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct db_ctx {
    PGconn *conn;
    char *conninfo;
};

static void log_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[db][warn] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static char *dup_str(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p)
        return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static char *build_conninfo(const char *host, const char *port, const char *dbname,
                            const char *user, const char *password) {
    /* Worst case: keyword=value pairs with spaces; allocate generously. */
    size_t cap = 512 + (host ? strlen(host) : 0) + (port ? strlen(port) : 0) +
                 (dbname ? strlen(dbname) : 0) + (user ? strlen(user) : 0) +
                 (password ? strlen(password) : 0);
    char *buf = (char *)malloc(cap);
    if (!buf)
        return NULL;
    snprintf(buf, cap,
             "host=%s port=%s dbname=%s user=%s password=%s "
             "connect_timeout=5",
             host ? host : "localhost", port ? port : "5432", dbname ? dbname : "appdb",
             user ? user : "app", password ? password : "");
    return buf;
}

db_ctx_t *db_connect(const char *host, const char *port, const char *dbname, const char *user,
                     const char *password) {
    db_ctx_t *ctx = (db_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    ctx->conninfo = build_conninfo(host, port, dbname, user, password);
    if (!ctx->conninfo) {
        free(ctx);
        return NULL;
    }
    ctx->conn = PQconnectdb(ctx->conninfo);
    if (PQstatus(ctx->conn) != CONNECTION_OK) {
        log_warn("initial connect failed: %s", PQerrorMessage(ctx->conn));
        /* Keep ctx alive for lazy reconnect. */
    }
    return ctx;
}

void db_free(db_ctx_t *ctx) {
    if (!ctx)
        return;
    if (ctx->conn)
        PQfinish(ctx->conn);
    free(ctx->conninfo);
    free(ctx);
}

bool db_ensure(db_ctx_t *ctx) {
    if (!ctx)
        return false;
    if (ctx->conn && PQstatus(ctx->conn) == CONNECTION_OK)
        return true;
    if (ctx->conn) {
        PQreset(ctx->conn);
        if (PQstatus(ctx->conn) == CONNECTION_OK)
            return true;
        PQfinish(ctx->conn);
        ctx->conn = NULL;
    }
    ctx->conn = PQconnectdb(ctx->conninfo);
    if (PQstatus(ctx->conn) != CONNECTION_OK) {
        log_warn("reconnect failed: %s", PQerrorMessage(ctx->conn));
        return false;
    }
    return true;
}

db_status_t db_ensure_schema(db_ctx_t *ctx) {
    if (!db_ensure(ctx))
        return DB_UNAVAILABLE;
    const char *sql = "CREATE TABLE IF NOT EXISTS data ("
                      "id SERIAL PRIMARY KEY, "
                      "content TEXT NOT NULL, "
                      "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW())";
    PGresult *r = PQexec(ctx->conn, sql);
    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_COMMAND_OK) {
        log_warn("ensure_schema failed: %s", PQerrorMessage(ctx->conn));
        PQclear(r);
        if (PQstatus(ctx->conn) != CONNECTION_OK)
            return DB_UNAVAILABLE;
        return DB_INTERNAL;
    }
    PQclear(r);
    return DB_OK;
}

void db_row_free(db_row_t *row) {
    if (!row)
        return;
    free(row->content);
    row->content = NULL;
    free(row->created_at);
    row->created_at = NULL;
    row->id = 0;
}

void db_rows_free(db_rows_t *rows) {
    if (!rows)
        return;
    for (size_t i = 0; i < rows->count; i++) {
        db_row_free(&rows->rows[i]);
    }
    free(rows->rows);
    rows->rows = NULL;
    rows->count = 0;
}

static bool row_from_pg(PGresult *r, int row_idx, db_row_t *out) {
    const char *id_s = PQgetvalue(r, row_idx, 0);
    const char *content_s = PQgetvalue(r, row_idx, 1);
    const char *created_s = PQgetvalue(r, row_idx, 2);
    out->id = id_s ? atol(id_s) : 0;
    out->content = dup_str(content_s ? content_s : "");
    out->created_at = dup_str(created_s ? created_s : "");
    if (!out->content || !out->created_at) {
        db_row_free(out);
        return false;
    }
    return true;
}

db_status_t db_query_all(db_ctx_t *ctx, db_rows_t *out) {
    if (!out)
        return DB_BAD_INPUT;
    out->rows = NULL;
    out->count = 0;
    if (!db_ensure(ctx))
        return DB_UNAVAILABLE;
    const char *sql = "SELECT id, content, "
                      "to_char(created_at AT TIME ZONE 'UTC', "
                      "'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"') "
                      "FROM data ORDER BY created_at DESC";
    PGresult *r = PQexec(ctx->conn, sql);
    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_TUPLES_OK) {
        log_warn("query_all failed: %s", PQerrorMessage(ctx->conn));
        PQclear(r);
        if (PQstatus(ctx->conn) != CONNECTION_OK)
            return DB_UNAVAILABLE;
        return DB_INTERNAL;
    }
    int n = PQntuples(r);
    if (n > 0) {
        out->rows = (db_row_t *)calloc((size_t)n, sizeof(db_row_t));
        if (!out->rows) {
            PQclear(r);
            return DB_INTERNAL;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!row_from_pg(r, i, &out->rows[i])) {
            PQclear(r);
            db_rows_free(out);
            return DB_INTERNAL;
        }
        out->count++;
    }
    PQclear(r);
    return DB_OK;
}

db_status_t db_query_one(db_ctx_t *ctx, long id, db_row_t *out) {
    if (!out)
        return DB_BAD_INPUT;
    memset(out, 0, sizeof(*out));
    if (!db_ensure(ctx))
        return DB_UNAVAILABLE;
    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "%ld", id);
    const char *vals[1] = {id_buf};
    int lens[1] = {(int)strlen(id_buf)};
    int fmts[1] = {0};
    const char *sql = "SELECT id, content, "
                      "to_char(created_at AT TIME ZONE 'UTC', "
                      "'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"') "
                      "FROM data WHERE id = $1::bigint";
    PGresult *r = PQexecParams(ctx->conn, sql, 1, NULL, vals, lens, fmts, 0);
    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_TUPLES_OK) {
        log_warn("query_one failed: %s", PQerrorMessage(ctx->conn));
        PQclear(r);
        if (PQstatus(ctx->conn) != CONNECTION_OK)
            return DB_UNAVAILABLE;
        return DB_INTERNAL;
    }
    int n = PQntuples(r);
    if (n == 0) {
        PQclear(r);
        return DB_NOT_FOUND;
    }
    bool ok = row_from_pg(r, 0, out);
    PQclear(r);
    return ok ? DB_OK : DB_INTERNAL;
}

db_status_t db_insert(db_ctx_t *ctx, const char *content, db_row_t *out) {
    if (!out || !content)
        return DB_BAD_INPUT;
    memset(out, 0, sizeof(*out));
    if (!db_ensure(ctx))
        return DB_UNAVAILABLE;
    const char *vals[1] = {content};
    int lens[1] = {(int)strlen(content)};
    int fmts[1] = {0};
    const char *sql = "INSERT INTO data (content) VALUES ($1) "
                      "RETURNING id, content, "
                      "to_char(created_at AT TIME ZONE 'UTC', "
                      "'YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"')";
    PGresult *r = PQexecParams(ctx->conn, sql, 1, NULL, vals, lens, fmts, 0);
    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_TUPLES_OK) {
        log_warn("insert failed: %s", PQerrorMessage(ctx->conn));
        PQclear(r);
        if (PQstatus(ctx->conn) != CONNECTION_OK)
            return DB_UNAVAILABLE;
        return DB_INTERNAL;
    }
    bool ok = (PQntuples(r) > 0) && row_from_pg(r, 0, out);
    PQclear(r);
    return ok ? DB_OK : DB_INTERNAL;
}

db_status_t db_find_user_by_email(db_ctx_t *ctx, const char *email, db_user_t *out) {
    if (!ctx || !email || !out)
        return DB_BAD_INPUT;
    memset(out, 0, sizeof(*out));
    if (!db_ensure(ctx))
        return DB_UNAVAILABLE;

    const char *vals[1] = {email};
    int lens[1] = {(int)strlen(email)};
    int fmts[1] = {0};
    const char *sql =
        "SELECT id, email, password_hash, COALESCE(to_jsonb(roles)::text, '[]') AS roles_json "
        "FROM users WHERE LOWER(email) = LOWER($1)";
    PGresult *r = PQexecParams(ctx->conn, sql, 1, NULL, vals, lens, fmts, 0);
    ExecStatusType st = PQresultStatus(r);
    if (st != PGRES_TUPLES_OK) {
        log_warn("find_user_by_email failed: %s", PQerrorMessage(ctx->conn));
        PQclear(r);
        if (PQstatus(ctx->conn) != CONNECTION_OK)
            return DB_UNAVAILABLE;
        return DB_INTERNAL;
    }
    if (PQntuples(r) == 0) {
        PQclear(r);
        return DB_NOT_FOUND;
    }
    out->id = strtol(PQgetvalue(r, 0, 0), NULL, 10);
    out->email = strdup(PQgetvalue(r, 0, 1));
    out->password_hash = strdup(PQgetvalue(r, 0, 2));
    out->roles_json = strdup(PQgetvalue(r, 0, 3));
    PQclear(r);
    if (!out->email || !out->password_hash || !out->roles_json) {
        db_user_free(out);
        return DB_INTERNAL;
    }
    return DB_OK;
}

void db_user_free(db_user_t *user) {
    if (!user)
        return;
    free(user->email);
    free(user->password_hash);
    free(user->roles_json);
    memset(user, 0, sizeof(*user));
}
