"""FastAPI application factory."""
from __future__ import annotations

import json
import logging
from typing import Any, Optional

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from .cache import Cache, cache_key_all, cache_key_item
from .config import Config, load_config
from .db import Database, DatabaseUnavailable

logger = logging.getLogger(__name__)


def parse_positive_int(raw: str) -> Optional[int]:
    """Return positive int from string, else None."""
    if raw is None:
        return None
    s = str(raw).strip()
    if not s:
        return None
    # disallow signs, decimals, whitespace inside
    if not s.isdigit():
        return None
    try:
        val = int(s)
    except ValueError:
        return None
    if val <= 0:
        return None
    return val


def validate_content(body: Any) -> Optional[str]:
    """Return the validated content string, or None if invalid."""
    if not isinstance(body, dict):
        return None
    content = body.get("content")
    if not isinstance(content, str):
        return None
    if content == "":
        return None
    return content


def _json(status: int, payload: Any) -> JSONResponse:
    return JSONResponse(status_code=status, content=payload)


def create_app(
    db: Optional[Any] = None,
    cache: Optional[Any] = None,
    config: Optional[Config] = None,
) -> FastAPI:
    cfg = config or load_config()
    database = db if db is not None else Database(cfg.dsn)
    cache_obj = (
        cache
        if cache is not None
        else Cache(
            host=cfg.redis_host,
            port=cfg.redis_port,
            timeout_ms=cfg.redis_timeout_ms,
            ttl_seconds=cfg.cache_ttl_seconds,
        )
    )

    app = FastAPI(title="data-api", version="1.0.0")
    app.state.config = cfg
    app.state.db = database
    app.state.cache = cache_obj
    app.state.schema_ready = False

    api_prefix = cfg.api_prefix
    max_body = cfg.max_body_bytes

    @app.on_event("startup")
    def _startup() -> None:
        try:
            database.ensure_schema()
            app.state.schema_ready = True
        except DatabaseUnavailable as exc:
            logger.warning("startup schema ensure failed (will retry on requests): %s", exc)
            app.state.schema_ready = False
        except Exception as exc:  # pragma: no cover - defensive
            logger.warning("startup ensure failed: %s", exc)
            app.state.schema_ready = False

    @app.on_event("shutdown")
    def _shutdown() -> None:
        try:
            if hasattr(database, "close"):
                database.close()
        except Exception:  # pragma: no cover
            pass
        try:
            if hasattr(cache_obj, "close"):
                cache_obj.close()
        except Exception:  # pragma: no cover
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

    @app.get("/health")
    def health() -> JSONResponse:
        return _json(200, {"status": "ok", "lang": cfg.app_lang})

    @app.get(f"{api_prefix}/data")
    def list_data() -> JSONResponse:
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
    def get_data(item_id: str) -> JSONResponse:
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
        # Content-Length check
        cl_header = request.headers.get("content-length")
        if cl_header is not None:
            try:
                cl = int(cl_header)
                if cl > max_body:
                    return _json(413, {"error": "payload too large"})
            except ValueError:
                pass

        # Read body with size limit (handles missing content-length / chunked)
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
            return _json(
                400,
                {"error": "content is required and must be a non-empty string"},
            )

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
app = create_app()
