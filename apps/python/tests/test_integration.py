"""Integration tests using TestClient with injected fake db and cache."""
from __future__ import annotations

from datetime import UTC, datetime, timezone
from typing import Any, Optional

import pytest
from fastapi.testclient import TestClient

from app.config import Config
from app.db import DatabaseUnavailable, DataItem
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

    def get_by_id(self, item_id: int) -> DataItem | None:
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
            created_at=datetime.now(UTC),
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

    def get_json(self, key: str) -> Any | None:
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

    def get_json(self, key: str) -> Any | None:
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

def make_config(
    lang: str = "python",
    max_body: int = 1024,
    api_key: str = "",
    api_key_next: str = "",
    jwt_secret: str = "",
) -> Config:
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
        redis_tls=False,
        max_body_bytes=max_body,
        api_key=api_key,
        api_key_next=api_key_next,
        jwt_secret=jwt_secret,
        cookie_secure=False,
        access_log_path="./access.log",
        access_log_max_bytes=10485760,
        access_log_backups=5,
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
        DataItem(id=1, content="seeded", created_at=datetime.now(UTC))
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
        DataItem(id=5, content="x", created_at=datetime.now(UTC))
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
        DataItem(id=1, content="x", created_at=datetime(2024, 1, 1, tzinfo=UTC))
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


# ---------- JWT cookie auth ----------

import time

import bcrypt as bcrypt_lib
import jwt as pyjwt

JWT_SECRET = "jwt-test-secret-aaaa"


def _hash(password: str) -> str:
    return bcrypt_lib.hashpw(password.encode(), bcrypt_lib.gensalt(4)).decode()


def make_user(id_=1, email="alice@example.com", password="pa$$w0rd", roles=None):
    if roles is None:
        roles = ["reader"]
    return {
        "id": id_,
        "email": email,
        "password_hash": _hash(password),
        "roles": roles,
        "_password": password,
    }


class UsersDB(FakeDB):
    def __init__(self, users):
        super().__init__()
        self._users = users

    def find_user_by_email(self, email):
        for u in self._users:
            if u["email"].lower() == email.lower():
                return {"id": u["id"], "email": u["email"], "password_hash": u["password_hash"], "roles": u["roles"]}
        return None


def auth_app(users):
    db = UsersDB(users)
    cache = FakeCache()
    return create_app(db=db, cache=cache, config=make_config(jwt_secret=JWT_SECRET))


def issue(sub, email, roles, secret=JWT_SECRET, **overrides):
    now = int(time.time())
    payload = {"sub": str(sub), "email": email, "roles": roles, "iat": now, "exp": now + 60}
    payload.update(overrides)
    return pyjwt.encode(payload, secret, algorithm="HS256")


def test_login_sets_session_cookie():
    u = make_user()
    app = auth_app([u])
    with TestClient(app) as c:
        r = c.post("/api/python/auth/login", json={"email": u["email"], "password": u["_password"]})
        assert r.status_code == 200, r.text
        assert r.json()["user"]["email"] == u["email"]
        cookie = r.headers["set-cookie"]
        assert "session=" in cookie
        assert "HttpOnly" in cookie
        assert "SameSite=Strict" in cookie


def test_login_wrong_password_returns_401():
    u = make_user()
    app = auth_app([u])
    with TestClient(app) as c:
        r = c.post("/api/python/auth/login", json={"email": u["email"], "password": "nope"})
        assert r.status_code == 401
        assert r.json() == {"error": "invalid credentials"}


def test_login_unknown_email_returns_401():
    app = auth_app([make_user()])
    with TestClient(app) as c:
        r = c.post("/api/python/auth/login", json={"email": "ghost@x.com", "password": "x"})
        assert r.status_code == 401


def test_login_missing_fields_returns_400():
    app = auth_app([make_user()])
    with TestClient(app) as c:
        r = c.post("/api/python/auth/login", json={})
        assert r.status_code == 400


def test_me_without_cookie_returns_401():
    app = auth_app([make_user()])
    with TestClient(app) as c:
        r = c.get("/api/python/auth/me")
        assert r.status_code == 401


def test_me_with_valid_cookie_returns_user():
    u = make_user(id_=42, roles=["admin"])
    app = auth_app([u])
    token = issue(u["id"], u["email"], u["roles"])
    with TestClient(app) as c:
        r = c.get("/api/python/auth/me", cookies={"session": token})
        assert r.status_code == 200
        assert r.json()["user"]["id"] == 42


def test_me_with_expired_cookie_returns_401():
    u = make_user()
    app = auth_app([u])
    now = int(time.time())
    expired = pyjwt.encode(
        {"sub": "1", "email": u["email"], "roles": u["roles"], "iat": now - 120, "exp": now - 60},
        JWT_SECRET, algorithm="HS256",
    )
    with TestClient(app) as c:
        r = c.get("/api/python/auth/me", cookies={"session": expired})
        assert r.status_code == 401


def test_me_with_tampered_cookie_returns_401():
    app = auth_app([make_user()])
    bad = pyjwt.encode({"sub": "1"}, "wrong-secret", algorithm="HS256")
    with TestClient(app) as c:
        r = c.get("/api/python/auth/me", cookies={"session": bad})
        assert r.status_code == 401


def test_get_data_requires_auth():
    app = auth_app([make_user()])
    with TestClient(app) as c:
        assert c.get("/api/python/data").status_code == 401


def test_get_data_succeeds_with_session():
    u = make_user(roles=["reader"])
    app = auth_app([u])
    with TestClient(app) as c:
        login = c.post("/api/python/auth/login", json={"email": u["email"], "password": u["_password"]})
        assert login.status_code == 200
        r = c.get("/api/python/data")
        assert r.status_code == 200
        assert r.json()["source"] == "db"


def test_post_data_forbidden_without_writer_role():
    u = make_user(roles=["reader"])
    app = auth_app([u])
    with TestClient(app) as c:
        c.post("/api/python/auth/login", json={"email": u["email"], "password": u["_password"]})
        r = c.post("/api/python/data", json={"content": "hi"})
        assert r.status_code == 403
        assert r.json() == {"error": "forbidden"}


def test_post_data_ok_when_user_is_writer():
    u = make_user(roles=["writer"])
    app = auth_app([u])
    with TestClient(app) as c:
        c.post("/api/python/auth/login", json={"email": u["email"], "password": u["_password"]})
        r = c.post("/api/python/data", json={"content": "hi"})
        assert r.status_code == 201


def test_logout_clears_session_cookie():
    app = auth_app([make_user()])
    with TestClient(app) as c:
        r = c.post("/api/python/auth/logout")
        assert r.status_code == 204
        assert "session=" in r.headers["set-cookie"]
        assert "Max-Age=0" in r.headers["set-cookie"]


# ---------- Edge cases ----------

def test_unicode_content_roundtrips(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(max_body=4096))
    with TestClient(app) as c:
        value = "hello 🌍 — résumé naïve façade Ω"
        r1 = c.post("/api/python/data", json={"content": value})
        assert r1.status_code == 201
        assert r1.json()["item"]["content"] == value
        item_id = r1.json()["item"]["id"]
        r2 = c.get(f"/api/python/data/{item_id}")
        assert r2.status_code == 200
        assert r2.json()["item"]["content"] == value


def test_content_with_quotes_and_backslashes(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        value = 'with "quotes" and \\backslash and \nnewline'
        r = c.post("/api/python/data", json={"content": value})
        assert r.status_code == 201
        assert r.json()["item"]["content"] == value


def test_body_at_max_body_bytes_boundary(fake_db, fake_cache):
    cap = 256
    overhead = len('{"content":""}')
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(max_body=cap))
    with TestClient(app) as c:
        exact = '{"content":"' + "x" * (cap - overhead) + '"}'
        assert len(exact) == cap
        r = c.post("/api/python/data", content=exact, headers={"content-type": "application/json"})
        assert r.status_code == 201
        bigger = '{"content":"' + "x" * (cap - overhead + 1) + '"}'
        r2 = c.post("/api/python/data", content=bigger, headers={"content-type": "application/json"})
        assert r2.status_code == 413


def test_delete_method_not_allowed_or_404(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        r = c.delete("/api/python/data")
        assert 400 <= r.status_code < 500


def test_unknown_language_prefix_returns_404(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        r = c.get("/api/rust/data")
        assert r.status_code == 404


def test_login_with_non_string_email_returns_400(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(jwt_secret="x"))
    with TestClient(app) as c:
        for bad in [{"email": 1, "password": "p"}, {"email": "a", "password": None}]:
            r = c.post("/api/python/auth/login", json=bad)
            assert r.status_code == 400


def test_jwt_malformed_tokens_return_401():
    app = auth_app([make_user()])
    with TestClient(app) as c:
        for bad in ["only.one", "a.b.c.d", "garbage", ".."]:
            r = c.get("/api/python/auth/me", cookies={"session": bad})
            assert r.status_code == 401


def test_api_key_header_case_insensitive(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config(api_key="k"))
    with TestClient(app) as c:
        r = c.get("/api/python/data", headers={"x-api-key": "k"})
        assert r.status_code != 401


def test_health_with_extra_path_returns_404(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        r = c.get("/health/extra")
        assert r.status_code == 404


def test_listing_serializes_created_at_as_iso_string(fake_db, fake_cache):
    fake_db.items.append(
        DataItem(id=99, content="x", created_at=datetime(2024, 5, 1, 12, 30, 0, tzinfo=UTC))
    )
    fake_db._next_id = 100
    app = create_app(db=fake_db, cache=fake_cache, config=make_config())
    with TestClient(app) as c:
        r = c.get("/api/python/data")
        assert r.status_code == 200
        item = r.json()["items"][0]
        assert item["created_at"].startswith("2024-05-01T12:30:00")


# ---------- API-key rotation ----------

def test_api_key_rotation_both_accepted(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache,
                     config=make_config(api_key="old", api_key_next="new"))
    with TestClient(app) as c:
        assert c.get("/api/python/data", headers={"X-API-Key": "old"}).status_code == 200
        assert c.get("/api/python/data", headers={"X-API-Key": "new"}).status_code == 200
        assert c.get("/api/python/data", headers={"X-API-Key": "nope"}).status_code == 401


def test_api_key_rotation_after_swap_only_new(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache,
                     config=make_config(api_key="new", api_key_next=""))
    with TestClient(app) as c:
        assert c.get("/api/python/data", headers={"X-API-Key": "new"}).status_code == 200
        assert c.get("/api/python/data", headers={"X-API-Key": "old"}).status_code == 401


def test_api_key_only_next_set_still_enforces(fake_db, fake_cache):
    app = create_app(db=fake_db, cache=fake_cache,
                     config=make_config(api_key="", api_key_next="new"))
    with TestClient(app) as c:
        assert c.get("/api/python/data", headers={"X-API-Key": "new"}).status_code == 200
        assert c.get("/api/python/data").status_code == 401
