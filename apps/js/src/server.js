'use strict';

const { createApp } = require('./app');
const { createDb } = require('./db');
const { createCache } = require('./cache');
const { buildAccessLog } = require('./access_log');

const PORT             = parseInt(process.env.PORT || '8080', 10);
const LANG             = process.env.APP_LANG || 'js';
const MAX_BODY_BYTES   = parseInt(process.env.MAX_BODY_BYTES || '1048576', 10);
const API_KEY          = process.env.API_KEY || '';
const JWT_SECRET       = process.env.JWT_SECRET || '';
const COOKIE_SECURE    = (process.env.COOKIE_SECURE || 'true').toLowerCase() !== 'false';
const ACCESS_LOG_PATH  = process.env.ACCESS_LOG_PATH || './access.log';
const ACCESS_LOG_MAX_BYTES = parseInt(process.env.ACCESS_LOG_MAX_BYTES || `${10 * 1024 * 1024}`, 10);
const ACCESS_LOG_BACKUPS   = parseInt(process.env.ACCESS_LOG_BACKUPS   || '5', 10);

async function main() {
  const db = createDb();
  const cache = createCache();

  scheduleSchemaBootstrap(db);

  if (!API_KEY)    console.warn('[server] API_KEY env var is empty — API-key auth is DISABLED.');
  if (!JWT_SECRET) console.warn('[server] JWT_SECRET env var is empty — JWT auth is DISABLED.');

  const accessLog = buildAccessLog({
    logPath: ACCESS_LOG_PATH,
    maxBytes: ACCESS_LOG_MAX_BYTES,
    backups: ACCESS_LOG_BACKUPS,
  });

  const app = createApp({
    db, cache,
    lang: LANG,
    maxBodyBytes: MAX_BODY_BYTES,
    apiKey: API_KEY,
    jwtSecret: JWT_SECRET,
    cookieSecure: COOKIE_SECURE,
    accessLog,
  });

  const server = app.listen(PORT, '0.0.0.0', () => {
    console.log(`[server] lang=${LANG} listening on 0.0.0.0:${PORT}`);
  });

  process.on('unhandledRejection', (reason) => {
    console.error('[server] unhandledRejection:', reason && reason.message ? reason.message : reason);
  });
  process.on('uncaughtException', (err) => {
    console.error('[server] uncaughtException:', err && err.message ? err.message : err);
  });

  const shutdown = async (signal) => {
    console.log(`[server] received ${signal}, shutting down`);
    server.close(() => {});
    try { await cache.close(); } catch (_) {}
    try { await db.close(); } catch (_) {}
    setTimeout(() => process.exit(0), 100).unref();
  };
  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT',  () => shutdown('SIGINT'));
}

function scheduleSchemaBootstrap(db, attempt = 0) {
  db.ensureSchema()
    .then(() => console.log('[server] schema ready'))
    .catch((err) => {
      const delay = Math.min(30000, 500 * Math.pow(2, attempt));
      console.warn(`[server] schema bootstrap failed (attempt ${attempt + 1}): ${err.message}; retrying in ${delay}ms`);
      try { db.reset && db.reset(); } catch (_) {}
      setTimeout(() => scheduleSchemaBootstrap(db, attempt + 1), delay).unref();
    });
}

main().catch((err) => {
  console.error('[server] fatal:', err.message);
  process.exit(1);
});
