#!/usr/bin/env bash
#
# Migrator container entrypoint. Runs `migrate.sh up` unconditionally, then
# `seed-admin` when ADMIN_EMAIL and ADMIN_PASSWORD are both set in the
# environment. seed-admin is an UPSERT (ON CONFLICT DO UPDATE), so re-running
# it on every deploy is safe and effectively rotates the admin password to
# whatever Secrets Manager currently holds.
#
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$DIR/migrate.sh" up

if [ -n "${ADMIN_EMAIL:-}" ] && [ -n "${ADMIN_PASSWORD:-}" ]; then
    echo "[entrypoint] seeding admin user: $ADMIN_EMAIL"
    "$DIR/migrate.sh" seed-admin --email "$ADMIN_EMAIL" --password "$ADMIN_PASSWORD"
else
    echo "[entrypoint] ADMIN_EMAIL / ADMIN_PASSWORD not set; skipping seed-admin"
fi
