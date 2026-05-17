# Migrator image — runs db/migrate.sh against the prod RDS from inside the
# VPC. Invoked by the CI workflow's `migrate` job via `aws ecs run-task`
# before any new app images are rolled out.
#
# Image contents:
#   - postgresql-client (for psql, used by migrate.sh)
#   - python3 + bcrypt (used by `migrate.sh seed-admin`)
#   - the db/ directory (migrations + the script itself)

FROM python:3.12-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        postgresql-client \
        ca-certificates \
        bash \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir bcrypt

# The build context is the repo root so we can pull db/ in.
WORKDIR /work
COPY db /work/db

# entrypoint.sh runs `migrate.sh up` then UPSERTs the admin user when
# ADMIN_EMAIL and ADMIN_PASSWORD are set in the env (injected by ECS from
# Secrets Manager).
ENTRYPOINT ["/work/db/entrypoint.sh"]
