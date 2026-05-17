"""Postgres pool, schema bootstrap, and query helpers."""
from __future__ import annotations

import logging
from dataclasses import dataclass
from datetime import UTC, datetime
from typing import Any

logger = logging.getLogger(__name__)


class DatabaseUnavailable(Exception):
    """Raised when the database cannot service a request."""


@dataclass
class DataItem:
    id: int
    content: str
    created_at: datetime

    def to_dict(self) -> dict[str, Any]:
        ts = self.created_at
        if ts.tzinfo is None:
            ts = ts.replace(tzinfo=UTC)
        return {
            "id": self.id,
            "content": self.content,
            "created_at": ts.isoformat(),
        }


SCHEMA_SQL = (
    "CREATE TABLE IF NOT EXISTS data ("
    "id SERIAL PRIMARY KEY, "
    "content TEXT NOT NULL, "
    "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()"
    ");"
)


class Database:
    """Thin wrapper around psycopg_pool.ConnectionPool with lazy init."""

    def __init__(self, dsn: str, *, min_size: int = 1, max_size: int = 10) -> None:
        self._dsn = dsn
        self._min_size = min_size
        self._max_size = max_size
        self._pool = None  # type: ignore[assignment]

    def _ensure_pool(self):
        if self._pool is not None:
            return self._pool
        try:
            from psycopg_pool import ConnectionPool

            pool = ConnectionPool(
                conninfo=self._dsn,
                min_size=self._min_size,
                max_size=self._max_size,
                open=False,
                kwargs={"autocommit": True},
            )
            pool.open(wait=True, timeout=5.0)
            self._pool = pool
            return pool
        except Exception as exc:  # pragma: no cover - exercised by error path
            logger.warning("postgres pool open failed: %s", exc)
            self._pool = None
            raise DatabaseUnavailable(str(exc)) from exc

    def close(self) -> None:
        if self._pool is not None:
            try:
                self._pool.close()
            except Exception:  # pragma: no cover
                pass
            self._pool = None

    def ensure_schema(self) -> None:
        try:
            pool = self._ensure_pool()
            with pool.connection() as conn:
                with conn.cursor() as cur:
                    cur.execute(SCHEMA_SQL)
        except DatabaseUnavailable:
            raise
        except Exception as exc:
            logger.warning("ensure_schema failed: %s", exc)
            # reset pool so next call retries cleanly
            self.close()
            raise DatabaseUnavailable(str(exc)) from exc

    def list_all(self) -> list[DataItem]:
        try:
            pool = self._ensure_pool()
            with pool.connection() as conn:
                with conn.cursor() as cur:
                    cur.execute(
                        "SELECT id, content, created_at FROM data "
                        "ORDER BY created_at DESC"
                    )
                    rows = cur.fetchall()
            return [DataItem(id=r[0], content=r[1], created_at=r[2]) for r in rows]
        except DatabaseUnavailable:
            raise
        except Exception as exc:
            logger.warning("list_all failed: %s", exc)
            self.close()
            raise DatabaseUnavailable(str(exc)) from exc

    def get_by_id(self, item_id: int) -> DataItem | None:
        try:
            pool = self._ensure_pool()
            with pool.connection() as conn:
                with conn.cursor() as cur:
                    cur.execute(
                        "SELECT id, content, created_at FROM data WHERE id = %s",
                        (item_id,),
                    )
                    row = cur.fetchone()
            if row is None:
                return None
            return DataItem(id=row[0], content=row[1], created_at=row[2])
        except DatabaseUnavailable:
            raise
        except Exception as exc:
            logger.warning("get_by_id failed: %s", exc)
            self.close()
            raise DatabaseUnavailable(str(exc)) from exc

    def insert(self, content: str) -> DataItem:
        try:
            pool = self._ensure_pool()
            with pool.connection() as conn:
                with conn.cursor() as cur:
                    cur.execute(
                        "INSERT INTO data (content) VALUES (%s) "
                        "RETURNING id, content, created_at",
                        (content,),
                    )
                    row = cur.fetchone()
            assert row is not None
            return DataItem(id=row[0], content=row[1], created_at=row[2])
        except DatabaseUnavailable:
            raise
        except Exception as exc:
            logger.warning("insert failed: %s", exc)
            self.close()
            raise DatabaseUnavailable(str(exc)) from exc

    def find_user_by_email(self, email: str) -> dict | None:
        try:
            pool = self._ensure_pool()
            with pool.connection() as conn:
                with conn.cursor() as cur:
                    cur.execute(
                        "SELECT id, email, password_hash, roles FROM users "
                        "WHERE LOWER(email) = LOWER(%s)",
                        (email,),
                    )
                    row = cur.fetchone()
            if row is None:
                return None
            return {
                "id": int(row[0]),
                "email": row[1],
                "password_hash": row[2],
                "roles": list(row[3] or []),
            }
        except DatabaseUnavailable:
            raise
        except Exception as exc:
            logger.warning("find_user_by_email failed: %s", exc)
            self.close()
            raise DatabaseUnavailable(str(exc)) from exc
