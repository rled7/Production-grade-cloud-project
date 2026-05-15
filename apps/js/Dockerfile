# syntax=docker/dockerfile:1.7
FROM node:20-slim AS builder

ENV NODE_ENV=production

WORKDIR /build

COPY package.json ./
# Use npm install (no lockfile shipped); --omit=dev keeps test deps out of the image.
RUN npm install --omit=dev --no-audit --no-fund \
    && npm cache clean --force


FROM node:20-slim AS runtime

ENV NODE_ENV=production \
    PORT=8080 \
    APP_LANG=js

RUN apt-get update \
    && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --system --gid 1001 appgroup \
    && useradd --system --uid 1001 --gid appgroup --home-dir /app --shell /usr/sbin/nologin appuser

WORKDIR /app

COPY --from=builder /build/node_modules /app/node_modules
COPY package.json /app/package.json
COPY src /app/src

RUN chown -R appuser:appgroup /app

USER appuser

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -fsS "http://127.0.0.1:${PORT}/health" || exit 1

CMD ["node", "src/server.js"]
