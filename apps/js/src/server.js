'use strict';

const { createApp } = require('./app');
const { createDb } = require('./db');
const { createCache } = require('./cache');

const PORT = parseInt(process.env.PORT || '8080', 10);
const LANG = process.env.APP_LANG || 'js';
const MAX_BODY_BYTES = parseInt(process.env.MAX_BODY_BYTES || '1048576', 10);

async function main() {
  const db = createDb();
  const cache = createCache();

  // Attempt schema bootstrap. Do not abort startup on failure — the next
  // request will surface a 503 from the regular DB error path, and a
  // background retry will keep trying.
  scheduleSchemaBootstrap(db);

  const app = createApp({ db, cache, lang: LANG, maxBodyBytes: MAX_BODY_BYTES });

  const server = app.listen(PORT, '0.0.0.0', () => {
    // eslint-disable-next-line no-console
    console.log(`[server] lang=${LANG} listening on 0.0.0.0:${PORT}`);
  });

  // Process-level safety nets so a stray rejection never kills the process.
  process.on('unhandledRejection', (reason) => {
    // eslint-disable-next-line no-console
    console.error('[server] unhandledRejection:', reason && reason.message ? reason.message : reason);
  });
  process.on('uncaughtException', (err) => {
    // eslint-disable-next-line no-console
    console.error('[server] uncaughtException:', err && err.message ? err.message : err);
  });

  const shutdown = async (signal) => {
    // eslint-disable-next-line no-console
    console.log(`[server] received ${signal}, shutting down`);
    server.close(() => {});
    try { await cache.close(); } catch (_) { /* ignore */ }
    try { await db.close(); } catch (_) { /* ignore */ }
    setTimeout(() => process.exit(0), 100).unref();
  };
  process.on('SIGTERM', () => shutdown('SIGTERM'));
  process.on('SIGINT', () => shutdown('SIGINT'));
}

function scheduleSchemaBootstrap(db, attempt = 0) {
  db.ensureSchema()
    .then(() => {
      // eslint-disable-next-line no-console
      console.log('[server] schema ready');
    })
    .catch((err) => {
      const delay = Math.min(30000, 500 * Math.pow(2, attempt));
      // eslint-disable-next-line no-console
      console.warn(
        `[server] schema bootstrap failed (attempt ${attempt + 1}): ${err.message}; retrying in ${delay}ms`
      );
      try { db.reset && db.reset(); } catch (_) { /* ignore */ }
      setTimeout(() => scheduleSchemaBootstrap(db, attempt + 1), delay).unref();
    });
}

main().catch((err) => {
  // eslint-disable-next-line no-console
  console.error('[server] fatal:', err.message);
  process.exit(1);
});
