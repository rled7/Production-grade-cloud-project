"""JWT session-cookie helpers."""
from __future__ import annotations

import time
from typing import Any

import jwt

COOKIE_NAME = "session"
TOKEN_TTL_SECONDS = 60 * 60  # 1 hour


def sign_session(claims: dict[str, Any], secret: str) -> str:
    now = int(time.time())
    payload = dict(claims)
    payload.setdefault("iat", now)
    payload["exp"] = now + TOKEN_TTL_SECONDS
    return jwt.encode(payload, secret, algorithm="HS256")


def verify_session(token: str, secret: str, secret_next: str = "") -> dict[str, Any] | None:
    """Verify against the primary secret first; fall back to secret_next for graceful rotation."""
    if secret:
        try:
            return jwt.decode(token, secret, algorithms=["HS256"])
        except jwt.PyJWTError:
            pass
    if secret_next:
        try:
            return jwt.decode(token, secret_next, algorithms=["HS256"])
        except jwt.PyJWTError:
            pass
    return None


def build_set_cookie(token: str, *, secure: bool, max_age: int = TOKEN_TTL_SECONDS) -> str:
    attrs = [
        f"{COOKIE_NAME}={token}",
        "HttpOnly",
        "SameSite=Strict",
        "Path=/",
        f"Max-Age={max_age}",
    ]
    if secure:
        attrs.append("Secure")
    return "; ".join(attrs)


def build_clear_cookie(*, secure: bool) -> str:
    attrs = [
        f"{COOKIE_NAME}=",
        "HttpOnly",
        "SameSite=Strict",
        "Path=/",
        "Max-Age=0",
    ]
    if secure:
        attrs.append("Secure")
    return "; ".join(attrs)


def has_role(roles: list[str], *required: str) -> bool:
    rset = set(roles or [])
    return any(r in rset for r in required)
