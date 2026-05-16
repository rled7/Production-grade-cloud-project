"""Rotating-file access log middleware for FastAPI."""
from __future__ import annotations

import logging
import logging.handlers
import os
import time
from typing import Awaitable, Callable

from starlette.requests import Request
from starlette.responses import Response


def build_access_logger(
    log_path: str = "./access.log",
    max_bytes: int = 10 * 1024 * 1024,
    backups: int = 5,
) -> logging.Logger:
    logger = logging.getLogger("access")
    logger.setLevel(logging.INFO)
    logger.propagate = False
    # Avoid attaching duplicate handlers on reload.
    if logger.handlers:
        return logger
    os.makedirs(os.path.dirname(log_path) or ".", exist_ok=True)
    file_handler = logging.handlers.RotatingFileHandler(
        log_path, maxBytes=max_bytes, backupCount=backups, encoding="utf-8"
    )
    file_handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(file_handler)
    # Mirror to stdout for CloudWatch capture.
    stream = logging.StreamHandler()
    stream.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(stream)
    return logger


def make_middleware(logger: logging.Logger) -> Callable:
    async def access_log_middleware(
        request: Request, call_next: Callable[[Request], Awaitable[Response]]
    ) -> Response:
        start = time.perf_counter()
        response = await call_next(request)
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        user_id = "-"
        if hasattr(request.state, "user") and request.state.user:
            user_id = str(request.state.user.get("sub", "-"))
        client = request.client.host if request.client else "-"
        size = response.headers.get("content-length", "-")
        logger.info(
            "%s - %s [%s] \"%s %s HTTP/%s\" %d %s - %.2f ms",
            client,
            user_id,
            time.strftime("%Y-%m-%dT%H:%M:%S%z", time.gmtime()),
            request.method,
            request.url.path,
            request.scope.get("http_version", "1.1"),
            response.status_code,
            size,
            elapsed_ms,
        )
        return response

    return access_log_middleware
