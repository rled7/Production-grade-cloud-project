-- V002: users table for JWT-cookie authentication.
--
-- password_hash holds a bcrypt $2b$ hash. roles is a flat array; the apps
-- recognize 'admin' (everything), 'writer' (POST /data), 'reader' (GET).

CREATE TABLE IF NOT EXISTS users (
    id            BIGSERIAL PRIMARY KEY,
    email         VARCHAR(255) NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    roles         TEXT[] NOT NULL DEFAULT ARRAY[]::TEXT[],
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_users_email_lower ON users (LOWER(email));
