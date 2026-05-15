-- Shared database schema for all four language implementations.
-- Applies identically to local docker-compose Postgres and production RDS.
-- Each application also runs this CREATE TABLE IF NOT EXISTS on startup for resiliency.

CREATE TABLE IF NOT EXISTS data (
    id          SERIAL PRIMARY KEY,
    content     TEXT NOT NULL,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_data_created_at ON data (created_at DESC);
