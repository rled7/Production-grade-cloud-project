"""Environment configuration parsing."""
from __future__ import annotations

import os
from dataclasses import dataclass


def _env_int(name: str, default: int) -> int:
    raw = os.environ.get(name)
    if raw is None or raw == "":
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def _env_str(name: str, default: str) -> str:
    raw = os.environ.get(name)
    if raw is None or raw == "":
        return default
    return raw


def _env_bool(name: str, default: bool) -> bool:
    raw = os.environ.get(name)
    if raw is None or raw == "":
        return default
    return raw.lower() not in ("false", "0", "no", "off")


@dataclass(frozen=True)
class Config:
    port: int
    app_lang: str
    db_host: str
    db_port: int
    db_name: str
    db_user: str
    db_password: str
    redis_host: str
    redis_port: int
    cache_ttl_seconds: int
    redis_timeout_ms: int
    redis_tls: bool
    max_body_bytes: int
    api_key: str  # empty string => API-key gate disabled
    api_key_next: str  # optional second key accepted during rotation
    jwt_secret: str  # empty string => JWT verification disabled
    cookie_secure: bool
    access_log_path: str
    access_log_max_bytes: int
    access_log_backups: int

    @property
    def redis_timeout_seconds(self) -> float:
        return self.redis_timeout_ms / 1000.0

    @property
    def api_prefix(self) -> str:
        return f"/api/{self.app_lang}"

    @property
    def dsn(self) -> str:
        return (
            f"host={self.db_host} port={self.db_port} dbname={self.db_name} "
            f"user={self.db_user} password={self.db_password}"
        )


def load_config() -> Config:
    return Config(
        port=_env_int("PORT", 8080),
        app_lang=_env_str("APP_LANG", "python"),
        db_host=_env_str("DB_HOST", "localhost"),
        db_port=_env_int("DB_PORT", 5432),
        db_name=_env_str("DB_NAME", "appdb"),
        db_user=_env_str("DB_USER", "app"),
        db_password=_env_str("DB_PASSWORD", ""),
        redis_host=_env_str("REDIS_HOST", "localhost"),
        redis_port=_env_int("REDIS_PORT", 6379),
        cache_ttl_seconds=_env_int("CACHE_TTL_SECONDS", 30),
        redis_timeout_ms=_env_int("REDIS_TIMEOUT_MS", 200),
        redis_tls=_env_bool("REDIS_TLS", False),
        max_body_bytes=_env_int("MAX_BODY_BYTES", 1048576),
        api_key=_env_str("API_KEY", ""),
        api_key_next=_env_str("API_KEY_NEXT", ""),
        jwt_secret=_env_str("JWT_SECRET", ""),
        cookie_secure=_env_bool("COOKIE_SECURE", True),
        access_log_path=_env_str("ACCESS_LOG_PATH", "./access.log"),
        access_log_max_bytes=_env_int("ACCESS_LOG_MAX_BYTES", 10 * 1024 * 1024),
        access_log_backups=_env_int("ACCESS_LOG_BACKUPS", 5),
    )
