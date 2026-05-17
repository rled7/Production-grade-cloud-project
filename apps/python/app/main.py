"""FastAPI application factory."""
from __future__ import annotations

import json
import logging
from typing import Any

import bcrypt
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response

from . import auth as auth_helpers
from .access_log import build_access_logger, make_middleware
from .cache import Cache, cache_key_all, cache_key_item
from .config import Config, load_config
from .db import Database, DatabaseUnavailable

logger = logging.getLogger(__name__)


def parse_positive_int(raw: str) -> int | None:
    if raw is None:
        return None
    s = str(raw).strip()
    if not s or not s.isdigit():
        return None
    try:
        val = int(s)
    except ValueError:
        return None
    if val <= 0:
        return None
    return val


def validate_content(body: Any) -> str | None:
    if not isinstance(body, dict):
        return None
    content = body.get("content")
    if not isinstance(content, str) or content == "":
        return None
    return content


def _json(status: int, payload: Any) -> JSONResponse:
    return JSONResponse(status_code=status, content=payload)


def create_app(
    db: Any | None = None,
    cache: Any | None = None,
    config: Config | None = None,
    api_key: str | None = None,
    api_key_next: str | None = None,
    jwt_secret: str | None = None,
    enable_access_log: bool = False,
) -> FastAPI:
    cfg = config or load_config()
    effective_api_key = api_key if api_key is not None else cfg.api_key
    effective_api_key_next = api_key_next if api_key_next is not None else cfg.api_key_next
    effective_jwt_secret = jwt_secret if jwt_secret is not None else cfg.jwt_secret
    cookie_secure = cfg.cookie_secure

    database = db if db is not None else Database(cfg.dsn)
    cache_obj = (
        cache
        if cache is not None
        else Cache(
            host=cfg.redis_host,
            port=cfg.redis_port,
            timeout_ms=cfg.redis_timeout_ms,
            ttl_seconds=cfg.cache_ttl_seconds,
            tls=cfg.redis_tls,
        )
    )

    app = FastAPI(title="data-api", version="1.1.0")
    app.state.config = cfg
    app.state.db = database
    app.state.cache = cache_obj
    app.state.schema_ready = False

    if enable_access_log:
        access_logger = build_access_logger(
            log_path=cfg.access_log_path,
            max_bytes=cfg.access_log_max_bytes,
            backups=cfg.access_log_backups,
        )
        app.middleware("http")(make_middleware(access_logger))

    api_prefix = cfg.api_prefix
    max_body = cfg.max_body_bytes

    # --------- API-key gate middleware ---------
    # During rotation, effective_api_key_next is also accepted.
    if effective_api_key or effective_api_key_next:

        @app.middleware("http")
        async def _api_key_auth(request: Request, call_next):
            if not request.url.path.startswith(api_prefix):
                return await call_next(request)
            presented = request.headers.get("x-api-key")
            if not presented:
                return _json(401, {"error": "missing api key"})
            ok = (
                (effective_api_key and presented == effective_api_key)
                or (effective_api_key_next and presented == effective_api_key_next)
            )
            if not ok:
                return _json(401, {"error": "invalid api key"})
            return await call_next(request)

    # --------- JWT session extraction ---------
    @app.middleware("http")
    async def _populate_user(request: Request, call_next):
        request.state.user = None
        if effective_jwt_secret:
            token = request.cookies.get(auth_helpers.COOKIE_NAME)
            if token:
                claims = auth_helpers.verify_session(token, effective_jwt_secret)
                if claims:
                    request.state.user = claims
        return await call_next(request)

    def require_auth(request: Request) -> JSONResponse | None:
        if not effective_jwt_secret:
            return None
        if not request.state.user:
            return _json(401, {"error": "authentication required"})
        return None

    def require_role(request: Request, *roles: str) -> JSONResponse | None:
        if not effective_jwt_secret:
            return None
        if not request.state.user:
            return _json(401, {"error": "authentication required"})
        user_roles = request.state.user.get("roles", []) or []
        if not auth_helpers.has_role(user_roles, *roles):
            return _json(403, {"error": "forbidden"})
        return None

    @app.on_event("startup")
    def _startup() -> None:
        try:
            database.ensure_schema()
            app.state.schema_ready = True
        except DatabaseUnavailable as exc:
            logger.warning("startup schema ensure failed: %s", exc)
            app.state.schema_ready = False
        except Exception as exc:
            logger.warning("startup ensure failed: %s", exc)
            app.state.schema_ready = False

    @app.on_event("shutdown")
    def _shutdown() -> None:
        try:
            if hasattr(database, "close"):
                database.close()
        except Exception:
            pass
        try:
            if hasattr(cache_obj, "close"):
                cache_obj.close()
        except Exception:
            pass

    def _ensure_schema_lazy() -> bool:
        if app.state.schema_ready:
            return True
        try:
            database.ensure_schema()
            app.state.schema_ready = True
            return True
        except DatabaseUnavailable:
            return False

    # --------- /health (public) ---------
    @app.get("/health")
    def health() -> JSONResponse:
        return _json(200, {"status": "ok", "lang": cfg.app_lang})

    # --------- Auth routes ---------
    @app.post(f"{api_prefix}/auth/login")
    async def login(request: Request) -> Response:
        try:
            body = await request.json()
        except Exception:
            return _json(400, {"error": "malformed json"})
        if not isinstance(body, dict):
            return _json(400, {"error": "malformed json"})
        email = body.get("email")
        password = body.get("password")
        if not isinstance(email, str) or not email.strip() or not isinstance(password, str) or not password:
            return _json(400, {"error": "email and password are required"})

        try:
            user = database.find_user_by_email(email.strip())
        except DatabaseUnavailable:
            return _json(503, {"error": "database unavailable"})

        if not user:
            return _json(401, {"error": "invalid credentials"})

        try:
            ok = bcrypt.checkpw(
                password.encode("utf-8"), user["password_hash"].encode("utf-8")
            )
        except Exception:
            ok = False
        if not ok:
            return _json(401, {"error": "invalid credentials"})

        if not effective_jwt_secret:
            return _json(500, {"error": "auth not configured"})

        token = auth_helpers.sign_session(
            {
                "sub": str(user["id"]),
                "email": user["email"],
                "roles": user["roles"],
            },
            effective_jwt_secret,
        )
        resp = _json(200, {"user": {
            "id": user["id"], "email": user["email"], "roles": user["roles"],
        }})
        resp.headers["set-cookie"] = auth_helpers.build_set_cookie(token, secure=cookie_secure)
        return resp

    @app.post(f"{api_prefix}/auth/logout")
    def logout() -> Response:
        resp = Response(status_code=204)
        resp.headers["set-cookie"] = auth_helpers.build_clear_cookie(secure=cookie_secure)
        return resp

    @app.get(f"{api_prefix}/auth/me")
    def me(request: Request) -> JSONResponse:
        guard = require_auth(request)
        if guard:
            return guard
        u = request.state.user
        return _json(200, {"user": {
            "id": int(u["sub"]),
            "email": u["email"],
            "roles": u.get("roles", []),
        }})

    # --------- Data routes (protected) ---------
    @app.get(f"{api_prefix}/data")
    def list_data(request: Request) -> JSONResponse:
        guard = require_auth(request)
        if guard:
            return guard
        cached = cache_obj.get_json(cache_key_all())
        if cached is not None and isinstance(cached, list):
            return _json(200, {"source": "cache", "items": cached})
        if not _ensure_schema_lazy():
            return _json(503, {"error": "database unavailable"})
        try:
            rows = database.list_all()
        except DatabaseUnavailable:
            return _json(503, {"error": "database unavailable"})
        items = [r.to_dict() if hasattr(r, "to_dict") else r for r in rows]
        cache_obj.set_json(cache_key_all(), items)
        return _json(200, {"source": "db", "items": items})

    @app.get(f"{api_prefix}/data/{{item_id}}")
    def get_data(request: Request, item_id: str) -> JSONResponse:
        guard = require_auth(request)
        if guard:
            return guard
        pid = parse_positive_int(item_id)
        if pid is None:
            return _json(400, {"error": "invalid id"})
        cached = cache_obj.get_json(cache_key_item(pid))
        if cached is not None and isinstance(cached, dict):
            return _json(200, {"source": "cache", "item": cached})
        if not _ensure_schema_lazy():
            return _json(503, {"error": "database unavailable"})
        try:
            row = database.get_by_id(pid)
        except DatabaseUnavailable:
            return _json(503, {"error": "database unavailable"})
        if row is None:
            return _json(404, {"error": "not found"})
        item = row.to_dict() if hasattr(row, "to_dict") else row
        cache_obj.set_json(cache_key_item(pid), item)
        return _json(200, {"source": "db", "item": item})

    @app.post(f"{api_prefix}/data")
    async def create_data(request: Request) -> JSONResponse:
        guard = require_role(request, "writer", "admin")
        if guard:
            return guard

        cl_header = request.headers.get("content-length")
        if cl_header is not None:
            try:
                if int(cl_header) > max_body:
                    return _json(413, {"error": "payload too large"})
            except ValueError:
                pass

        body_bytes = b""
        try:
            async for chunk in request.stream():
                body_bytes += chunk
                if len(body_bytes) > max_body:
                    return _json(413, {"error": "payload too large"})
        except Exception:
            return _json(400, {"error": "malformed json"})

        if not body_bytes:
            return _json(400, {"error": "malformed json"})
        try:
            parsed = json.loads(body_bytes.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return _json(400, {"error": "malformed json"})

        content = validate_content(parsed)
        if content is None:
            return _json(400, {"error": "content is required and must be a non-empty string"})

        if not _ensure_schema_lazy():
            return _json(503, {"error": "database unavailable"})
        try:
            row = database.insert(content)
        except DatabaseUnavailable:
            return _json(503, {"error": "database unavailable"})

        cache_obj.delete(cache_key_all())
        item = row.to_dict() if hasattr(row, "to_dict") else row
        return _json(201, {"item": item})

    return app


# Module-level app for `uvicorn app.main:app`
app = create_app(enable_access_log=True)
