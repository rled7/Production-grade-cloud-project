"""Unit tests for pure helpers: validation, id parsing, serialization, cache keys."""
from __future__ import annotations

from datetime import UTC, datetime, timezone

from app.cache import cache_key_all, cache_key_item
from app.config import load_config
from app.db import DataItem
from app.main import parse_positive_int, validate_content

# ---------- parse_positive_int ----------

def test_parse_positive_int_valid():
    assert parse_positive_int("1") == 1
    assert parse_positive_int("42") == 42
    assert parse_positive_int("1000000") == 1000000


def test_parse_positive_int_zero_is_invalid():
    assert parse_positive_int("0") is None


def test_parse_positive_int_negative_is_invalid():
    assert parse_positive_int("-1") is None
    assert parse_positive_int("-100") is None


def test_parse_positive_int_non_numeric():
    assert parse_positive_int("abc") is None
    assert parse_positive_int("12a") is None
    assert parse_positive_int("1.5") is None
    assert parse_positive_int("") is None
    assert parse_positive_int(" ") is None


def test_parse_positive_int_none():
    assert parse_positive_int(None) is None  # type: ignore[arg-type]


def test_parse_positive_int_whitespace_padding():
    # strip allowed, then isdigit check
    assert parse_positive_int("  5  ") == 5


# ---------- validate_content ----------

def test_validate_content_valid_string():
    assert validate_content({"content": "hello"}) == "hello"
    assert validate_content({"content": "a"}) == "a"


def test_validate_content_empty_string():
    assert validate_content({"content": ""}) is None


def test_validate_content_missing_key():
    assert validate_content({}) is None
    assert validate_content({"other": "x"}) is None


def test_validate_content_non_string():
    assert validate_content({"content": 123}) is None
    assert validate_content({"content": None}) is None
    assert validate_content({"content": ["x"]}) is None
    assert validate_content({"content": {"a": 1}}) is None
    assert validate_content({"content": True}) is None


def test_validate_content_non_dict_body():
    assert validate_content(None) is None
    assert validate_content("string") is None
    assert validate_content([1, 2]) is None
    assert validate_content(42) is None


# ---------- cache key builders ----------

def test_cache_key_all():
    assert cache_key_all() == "data:all"


def test_cache_key_item():
    assert cache_key_item(1) == "data:1"
    assert cache_key_item(42) == "data:42"
    assert cache_key_item(999999) == "data:999999"


# ---------- DataItem serialization ----------

def test_dataitem_to_dict_with_tz():
    ts = datetime(2024, 1, 2, 3, 4, 5, tzinfo=UTC)
    item = DataItem(id=7, content="hi", created_at=ts)
    d = item.to_dict()
    assert d["id"] == 7
    assert d["content"] == "hi"
    assert d["created_at"] == "2024-01-02T03:04:05+00:00"


def test_dataitem_to_dict_naive_treated_as_utc():
    ts = datetime(2024, 6, 15, 12, 0, 0)  # naive
    item = DataItem(id=1, content="x", created_at=ts)
    d = item.to_dict()
    # Should add UTC timezone
    assert d["created_at"].endswith("+00:00")
    assert "2024-06-15T12:00:00" in d["created_at"]


# ---------- config ----------

def test_load_config_defaults(monkeypatch):
    # Clear all relevant env vars
    for k in [
        "PORT", "APP_LANG", "DB_HOST", "DB_PORT", "DB_NAME", "DB_USER",
        "DB_PASSWORD", "REDIS_HOST", "REDIS_PORT", "CACHE_TTL_SECONDS",
        "REDIS_TIMEOUT_MS", "MAX_BODY_BYTES",
    ]:
        monkeypatch.delenv(k, raising=False)
    cfg = load_config()
    assert cfg.port == 8080
    assert cfg.app_lang == "python"
    assert cfg.db_port == 5432
    assert cfg.redis_port == 6379
    assert cfg.cache_ttl_seconds == 30
    assert cfg.redis_timeout_ms == 200
    assert cfg.max_body_bytes == 1048576
    assert cfg.api_prefix == "/api/python"
    assert cfg.redis_timeout_seconds == 0.2


def test_load_config_env_overrides(monkeypatch):
    monkeypatch.setenv("PORT", "9090")
    monkeypatch.setenv("APP_LANG", "py")
    monkeypatch.setenv("MAX_BODY_BYTES", "2048")
    monkeypatch.setenv("REDIS_TIMEOUT_MS", "500")
    cfg = load_config()
    assert cfg.port == 9090
    assert cfg.app_lang == "py"
    assert cfg.api_prefix == "/api/py"
    assert cfg.max_body_bytes == 2048
    assert cfg.redis_timeout_ms == 500


def test_load_config_invalid_int_falls_back(monkeypatch):
    monkeypatch.setenv("PORT", "not-a-number")
    cfg = load_config()
    assert cfg.port == 8080
