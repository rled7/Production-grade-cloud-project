# Multi-stage build for the C++ API.

FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libpqxx-dev \
        libhiredis-dev \
        libasio-dev \
        libssl-dev \
        libcrypt-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY src ./src
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --target server -j

FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
        libpqxx-6.4 \
        libhiredis0.14 \
        libssl3 \
        libcrypt1 \
        curl \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system app && useradd --system --gid app --create-home app

WORKDIR /app
COPY --from=builder /build/build/server /app/server
RUN chown -R app:app /app \
    && mkdir -p /var/log/app && chown app:app /var/log/app
USER app

EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=3s --start-period=10s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/health || exit 1

ENV PORT=8080 APP_LANG=cpp
ENTRYPOINT ["/app/server"]
