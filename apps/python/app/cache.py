"""Redis cache wrapper with graceful degradation."""
from __future__ import annotations

import json
import logging
from typing import Any, Optional

logger = logging.getLogger(__name__)


def cache_key_all() -> str:
    return "data:all"


def cache_key_item(item_id: int) -> str:
    return f"data:{item_id}"


class Cache:
    """Redis cache. All operations swallow errors and log warnings."""

    def __init__(
        self,
        host: str,
        port: int,
        timeout_ms: int,
        ttl_seconds: int,
    ) -> None:
        self._host = host
        self._port = port
        self._timeout = timeout_ms / 1000.0
        self._ttl = ttl_seconds
        self._client = None  # type: ignore[assignment]
        self._init_failed = False

    def _ensure_client(self):
        if self._client is not None:
            return self._client
        if self._init_failed:
            return None
        try:
            import redis as redis_lib

            client = redis_lib.Redis(
                host=self._host,
                port=self._port,
                socket_connect_timeout=self._timeout,
                socket_timeout=self._timeout,
                decode_responses=True,
            )
            self._client = client
            return client
        except Exception as exc:
            logger.warning("redis init failed, cache disabled: %s", exc)
            self._init_failed = True
            return None

    def get_json(self, key: str) -> Optional[Any]:
        client = self._ensure_client()
        if client is None:
            return None
        try:
            raw = client.get(key)
            if raw is None:
                return None
            return json.loads(raw)
        except Exception as exc:
            logger.warning("redis GET %s failed: %s", key, exc)
            return None

    def set_json(self, key: str, value: Any) -> None:
        client = self._ensure_client()
        if client is None:
            return
        try:
            client.set(key, json.dumps(value), ex=self._ttl)
        except Exception as exc:
            logger.warning("redis SET %s failed: %s", key, exc)

    def delete(self, key: str) -> None:
        client = self._ensure_client()
        if client is None:
            return
        try:
            client.delete(key)
        except Exception as exc:
            logger.warning("redis DEL %s failed: %s", key, exc)

    def close(self) -> None:
        if self._client is not None:
            try:
                self._client.close()
            except Exception:  # pragma: no cover
                pass
            self._client = None
