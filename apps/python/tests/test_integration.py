"""Integration tests using TestClient with injected fake db and cache."""
from __future__ import annotations

from datetime import datetime, timezone
from typing import Any, Optional

import pytest
from fastapi.testclient import TestClient

from app.config import Config
from app.db import DataItem, DatabaseUnavailable
from app.main import create_app


# ---------- Fakes ----------

class FakeDB:
    def __init__(self):
        self.items: list[DataItem] = []
        self._next_id = 1
        self.unavailable = False
        self.schema_called = 0

    def ensure_schema(self) -> None:
        self.schema_called += 1
        if self.unavailable:
            raise DatabaseUnavailable("simulated outage")

    def list_all(self) -> list[DataItem]:
        if self.unavailable:
            raise DatabaseUnavailable("simulated outage")
        return sorted(self.items, key=lambda r: r.created_at, reverse=True)

    def get_by_id(self, item_id: int) -> Optional[DataItem]:
        if self.unavailable:
            raise DatabaseUnavailable("simulated outage")
        for r in self.items:
            if r.id == item_id:
                return r
        return None

    def insert(self, content: str) -> DataItem:
        if self.unavailable:
            raise DatabaseUnavailable("simulated outage")
        row = DataItem(
            id=self._next_id,
            content=content,
            created_at=datetime.now(timezone.utc),
        )
        self._next_id += 1
        self.items.append(row)
        return row

    def close(self) -> None:
        pass


class FakeCache:
    """In-memory fake cache, behaves like the real one's surface."""

    def __init__(self):
        self.store: dict[str, Any] = {}
        self.get_calls: list[str] = []
        self.set_calls: list[str] = []
        self.delete_calls: list[str] = []

    def get_json(self, key: str) -> Optional[Any]:
        self.get_calls.append(key)
        return self.store.get(key)

    def set_json(self, key: str, value: Any) -> None:
        self.set_calls.append(key)
        self.store[key] = value

    def delete(self, key: str) -> None:
        self.delete_calls.append(key)
        self.store.pop(key, None)

    def close(self) -> None:
        pass


class BrokenCache:
    """Cache that simulates Redis being totally unavailable / timing out.

    The real Cache class catches exceptions internally and returns None/no-ops.
    This fake mirrors that contract: every op logs/skips and never raises.
    """

    def __init__(self):
        self.get_calls = 0
        self.set_calls = 0
        self.delete_calls = 0

    def get_json(self, key: str) -> Optional[Any]:
        self.get_calls += 1
        # Simulate "timeout caught, return None"
        return None

    def set_json(self, key: str, value: Any) -> None:
        self.set_calls += 1
        return None

    def delete(self, key: str) -> None:
        self.delete_calls += 1
        return None

    def close(self) -> None:
        pass


# ---------- Fixtures ----------

def make_config(lang: str = "python", max_body: int = 1024, api_key: str = "") -> Config:
    return Config(
        port=8080,
        app_lang=lang,
        db_host="x",
        db_port=5432,
        db_name="x",
        db_user="x",
        db_password="x",
        redis_host="x",
        redis_port=6379,
        cache_ttl_seconds=30,
        redis_timeout_ms=200,
        max_body_bytes=max_body,
        api_key=api_key,
    )


@pytest.fixture
def fake_db():
    return FakeDB()


@pytest.fixture
def fake_cache():
    return FakeCache()


@pytest.fixture
def client(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        yield c


# ---------- /health ----------

def test_health_ok(client):
    r = client.get("/health")
    assert r.status_code == 200
    assert r.json() == {"status": "ok", "lang": "python"}


def test_health_respects_app_lang(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(lang="rust"))
    with TestClient(app) as c:
        r = c.get("/health")
        assert r.status_code == 200
        assert r.json() == {"status": "ok", "lang": "rust"}


# ---------- POST /api/python/data ----------

def test_post_creates_item(client, fake_db, fake_cache):
    r = client.post("/api/python/data", json={"content": "hello"})
    assert r.status_code == 201
    body = r.json()
    assert body["item"]["content"] == "hello"
    assert body["item"]["id"] == 1
    assert "created_at" in body["item"]
    # cache:all invalidated
    assert "data:all" in fake_cache.delete_calls


def test_post_malformed_json_returns_400(client):
    r = client.post(
        "/api/python/data",
        data="{not json",
        headers={"content-type": "application/json"},
    )
    assert r.status_code == 400
    assert r.json() == {"error": "malformed json"}


def test_post_empty_body_returns_400(client):
    r = client.post(
        "/api/python/data",
        data="",
        headers={"content-type": "application/json"},
    )
    assert r.status_code == 400
    assert r.json() == {"error": "malformed json"}


def test_post_missing_content_returns_400(client):
    r = client.post("/api/python/data", json={})
    assert r.status_code == 400
    assert r.json() == {
        "error": "content is required and must be a non-empty string"
    }


def test_post_empty_content_returns_400(client):
    r = client.post("/api/python/data", json={"content": ""})
    assert r.status_code == 400
    assert r.json() == {
        "error": "content is required and must be a non-empty string"
    }


def test_post_non_string_content_returns_400(client):
    r = client.post("/api/python/data", json={"content": 123})
    assert r.status_code == 400
    assert r.json() == {
        "error": "content is required and must be a non-empty string"
    }


def test_post_oversized_body_returns_413(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(max_body=64))
    with TestClient(app) as c:
        # build a body bigger than 64 bytes
        big = "x" * 200
        r = c.post("/api/python/data", json={"content": big})
        assert r.status_code == 413
        assert r.json() == {"error": "payload too large"}


def test_post_db_unavailable_returns_503(fake_cache):
    db = FakeDB()
    db.unavailable = True
    app = create_app(db=db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        r = c.post("/api/python/data", json={"content": "x"})
        assert r.status_code == 503
        assert r.json() == {"error": "database unavailable"}


# ---------- GET /api/python/data ----------

def test_get_list_from_db_then_cache(client, fake_db, fake_cache):
    # seed db
    client.post("/api/python/data", json={"content": "a"})
    client.post("/api/python/data", json={"content": "b"})
    r1 = client.get("/api/python/data")
    assert r1.status_code == 200
    body1 = r1.json()
    assert body1["source"] == "db"
    assert len(body1["items"]) == 2
    # cached now; next call should be cache
    r2 = client.get("/api/python/data")
    assert r2.status_code == 200
    body2 = r2.json()
    assert body2["source"] == "cache"
    assert len(body2["items"]) == 2


def test_get_list_empty(client):
    r = client.get("/api/python/data")
    assert r.status_code == 200
    body = r.json()
    assert body["source"] == "db"
    assert body["items"] == []


# ---------- GET /api/python/data/{id} ----------

def test_get_by_id_invalid(client):
    for bad in ["0", "-1", "abc", "1.5", "%20"]:
        r = client.get(f"/api/python/data/{bad}")
        assert r.status_code == 400, f"id={bad}"
        assert r.json() == {"error": "invalid id"}


def test_get_by_id_not_found(client):
    r = client.get("/api/python/data/9999")
    assert r.status_code == 404
    assert r.json() == {"error": "not found"}


def test_get_by_id_returns_item_and_caches(client, fake_cache):
    post = client.post("/api/python/data", json={"content": "abc"}).json()
    item_id = post["item"]["id"]
    r1 = client.get(f"/api/python/data/{item_id}")
    assert r1.status_code == 200
    body1 = r1.json()
    assert body1["source"] == "db"
    assert body1["item"]["content"] == "abc"
    # second hit should be from cache
    r2 = client.get(f"/api/python/data/{item_id}")
    assert r2.status_code == 200
    assert r2.json()["source"] == "cache"


def test_get_by_id_db_unavailable_returns_503(fake_cache):
    db = FakeDB()
    db.unavailable = True
    app = create_app(db=db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        r = c.get("/api/python/data/1")
        assert r.status_code == 503


# ---------- Redis outage: GET endpoints must still serve from db (200) ----------

def test_get_list_redis_outage_returns_db_200(fake_db):
    """Critical: Redis timeout/outage must NOT cause 5xx; serves from db."""
    broken = BrokenCache()
    fake_db.items.append(
        DataItem(id=1, content="seeded", created_at=datetime.now(timezone.utc))
    )
    app = create_app(db=fake_db, cache=broken, config=make_config())
    with TestClient(app) as c:
        r = c.get("/api/python/data")
        assert r.status_code == 200, r.text
        body = r.json()
        assert body["source"] == "db"
        assert len(body["items"]) == 1
        assert body["items"][0]["content"] == "seeded"
    assert broken.get_calls >= 1


def test_get_by_id_redis_outage_returns_db_200(fake_db):
    broken = BrokenCache()
    fake_db.items.append(
        DataItem(id=5, content="x", created_at=datetime.now(timezone.utc))
    )
    fake_db._next_id = 6
    app = create_app(db=fake_db, cache=broken, config=make_config())
    with TestClient(app) as c:
        r = c.get("/api/python/data/5")
        assert r.status_code == 200, r.text
        body = r.json()
        assert body["source"] == "db"
        assert body["item"]["content"] == "x"


def test_post_with_redis_outage_still_succeeds(fake_db):
    broken = BrokenCache()
    app = create_app(db=fake_db, cache=broken, config=make_config())
    with TestClient(app) as c:
        r = c.post("/api/python/data", json={"content": "hi"})
        assert r.status_code == 201
        assert r.json()["item"]["content"] == "hi"
    # cache delete was attempted (and silently swallowed)
    assert broken.delete_calls >= 1


# ---------- API-key auth ----------

API_KEY = "test-key-abc123"


def test_health_works_without_api_key(fake_db, fake_cache):
    """ALB health-check path is exempt from auth."""
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key=API_KEY))
    with TestClient(app) as c:
        r = c.get("/health")
        assert r.status_code == 200
        assert r.json() == {"status": "ok", "lang": "python"}


def test_data_route_returns_401_without_key(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key=API_KEY))
    with TestClient(app) as c:
        r = c.get("/api/python/data")
        assert r.status_code == 401
        assert r.json() == {"error": "missing api key"}


def test_data_route_returns_401_with_wrong_key(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key=API_KEY))
    with TestClient(app) as c:
        r = c.get("/api/python/data", headers={"X-API-Key": "nope"})
        assert r.status_code == 401
        assert r.json() == {"error": "invalid api key"}


def test_data_route_returns_200_with_correct_key(fake_db, fake_cache):
    fake_db.items.append(
        DataItem(id=1, content="x", created_at=datetime(2024, 1, 1, tzinfo=timezone.utc))
    )
    fake_db._next_id = 2
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key=API_KEY))
    with TestClient(app) as c:
        r = c.get("/api/python/data", headers={"X-API-Key": API_KEY})
        assert r.status_code == 200
        body = r.json()
        assert body["source"] == "db"
        assert len(body["items"]) == 1


def test_post_data_also_requires_key(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key=API_KEY))
    with TestClient(app) as c:
        r1 = c.post("/api/python/data", json={"content": "hi"})
        assert r1.status_code == 401
        r2 = c.post(
            "/api/python/data",
            json={"content": "hi"},
            headers={"X-API-Key": API_KEY},
        )
        assert r2.status_code == 201


def test_empty_api_key_disables_auth(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key=""))
    with TestClient(app) as c:
        r = c.get("/api/python/data")
        assert r.status_code == 200


def test_api_key_override_param_overrides_config(fake_db, fake_cache):
    # config has empty key, but the explicit create_app(api_key=...) wins.
    app = create_app(
        db=fake_db,
        cache=fake_cache,
        config=make_config(api_key=""),
        api_key=API_KEY,
    )
    with TestClient(app) as c:
        r = c.get("/api/python/data")
        assert r.status_code == 401
