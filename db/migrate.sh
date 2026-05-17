#!/usr/bin/env bash
#
# Canonical Postgres migration runner. Applies any V*__*.sql file under
# db/migrations/ that hasn't already been recorded in schema_migrations.
#
# Connection is read from the standard PG* env vars:
#   PGHOST, PGPORT, PGUSER, PGPASSWORD, PGDATABASE
#
# Commands:
#   ./migrate.sh up
#     Apply every pending migration in version order. Each migration runs in
#     its own transaction together with the INSERT into schema_migrations.
#
#   ./migrate.sh status
#     List applied vs pending migrations.
#
#   ./migrate.sh seed-admin --email <e> --password <p>
#     Insert (or update) a user with roles {admin, writer, reader}, bcrypt
#     hashing the password via python3. Idempotent on email.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MIGRATIONS_DIR="$SCRIPT_DIR/migrations"

require_psql() {
    command -v psql >/dev/null 2>&1 || {
        echo "FATAL: psql is required on PATH." >&2
        exit 1
    }
}

ensure_tracker_table() {
    psql -v ON_ERROR_STOP=1 -q <<'SQL' >/dev/null
CREATE TABLE IF NOT EXISTS schema_migrations (
    version    VARCHAR(64) PRIMARY KEY,
    applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
SQL
}

migration_version() {
    # Extract Vnnn from e.g. V001__init.sql
    basename "$1" | sed -E 's/^(V[0-9]+)__.*/\1/'
}

is_applied() {
    local version="$1"
    local count
    count=$(psql -tA -c "SELECT COUNT(*) FROM schema_migrations WHERE version='${version}'")
    [ "$count" = "1" ]
}

cmd_up() {
    require_psql
    ensure_tracker_table
    local applied=0
    for f in $(ls -1 "$MIGRATIONS_DIR"/V*__*.sql 2>/dev/null | sort); do
        local v
        v="$(migration_version "$f")"
        if is_applied "$v"; then
            echo "    $v already applied"
            continue
        fi
        echo "==> applying $v ($(basename "$f"))"
        psql -v ON_ERROR_STOP=1 -1 -q <<EOF >/dev/null
$(cat "$f")
INSERT INTO schema_migrations (version) VALUES ('${v}');
EOF
        applied=$((applied + 1))
    done
    echo "==> done. ${applied} migration(s) applied."
}

cmd_status() {
    require_psql
    ensure_tracker_table
    echo "version    applied"
    echo "---------  -------"
    for f in $(ls -1 "$MIGRATIONS_DIR"/V*__*.sql 2>/dev/null | sort); do
        local v
        v="$(migration_version "$f")"
        if is_applied "$v"; then
            printf "%-10s yes\n" "$v"
        else
            printf "%-10s NO\n" "$v"
        fi
    done
}

cmd_seed_admin() {
    require_psql
    local email="" password=""
    while [ $# -gt 0 ]; do
        case "$1" in
            --email)    email="$2"; shift 2 ;;
            --password) password="$2"; shift 2 ;;
            *) echo "unknown arg: $1" >&2; exit 2 ;;
        esac
    done
    [ -z "$email" ]    && { echo "--email required" >&2; exit 2; }
    [ -z "$password" ] && { echo "--password required" >&2; exit 2; }

    command -v python3 >/dev/null 2>&1 || {
        echo "FATAL: python3 with bcrypt is required to hash passwords." >&2
        exit 1
    }

    local hash
    hash=$(python3 -c '
import sys, bcrypt
print(bcrypt.hashpw(sys.argv[1].encode("utf-8"), bcrypt.gensalt(12)).decode("ascii"))
' "$password")

    # Use a parameterized psql variable to avoid escaping concerns on the hash
    # and email. Hash contains $ characters; -v binds them safely as :hash.
    PGOPTIONS='--client-min-messages=warning' \
    psql -v ON_ERROR_STOP=1 -v "email=$email" -v "hash=$hash" -q <<'SQL' >/dev/null
INSERT INTO users (email, password_hash, roles)
VALUES (:'email', :'hash', ARRAY['admin','writer','reader'])
ON CONFLICT (email) DO UPDATE
   SET password_hash = EXCLUDED.password_hash,
       roles         = EXCLUDED.roles;
SQL
    echo "==> seeded admin: $email"
}

main() {
    local cmd="${1:-up}"
    shift || true
    case "$cmd" in
        up)         cmd_up "$@" ;;
        status)     cmd_status "$@" ;;
        seed-admin) cmd_seed_admin "$@" ;;
        -h|--help|help)
            cat <<EOF
usage: $0 <command>

commands:
  up                                   apply all pending migrations (default)
  status                               list applied vs pending migrations
  seed-admin --email <e> --password <p>  insert/update admin user (bcrypt)
EOF
            ;;
        *) echo "unknown command: $cmd" >&2; exit 2 ;;
    esac
}

main "$@"
