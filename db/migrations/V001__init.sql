-- V001: initial schema — the data table and the schema_migrations tracker.
-- The schema_migrations table is created by db/migrate.sh before any
-- migration runs, so don't recreate it here.

CREATE TABLE IF NOT EXISTS data (
    id          SERIAL PRIMARY KEY,
    content     TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_data_created_at ON data (created_at DESC);
