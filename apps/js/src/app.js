'use strict';

const express = require('express');
const { itemKey, ALL_KEY } = require('./cache');

const POSITIVE_INT_RE = /^[1-9][0-9]*$/;

function parsePositiveInt(raw) {
  if (typeof raw !== 'string') return null;
  if (!POSITIVE_INT_RE.test(raw)) return null;
  const n = Number(raw);
  if (!Number.isSafeInteger(n) || n <= 0) return null;
  return n;
}

function validateContent(body) {
  if (!body || typeof body !== 'object') {
    return { ok: false, error: 'content is required and must be a non-empty string' };
  }
  const c = body.content;
  if (typeof c !== 'string' || c.length === 0) {
    return { ok: false, error: 'content is required and must be a non-empty string' };
  }
  return { ok: true, content: c };
}

function isDbUnavailable(err) {
  if (!err) return false;
  const code = err.code;
  // pg error codes for connection-level problems
  if (
    code === 'ECONNREFUSED' ||
    code === 'ENOTFOUND' ||
    code === 'ETIMEDOUT' ||
    code === 'ECONNRESET' ||
    code === 'EHOSTUNREACH' ||
    code === 'EAI_AGAIN' ||
    code === '57P01' || // admin_shutdown
    code === '57P02' || // crash_shutdown
    code === '57P03' || // cannot_connect_now
    code === '08000' || // connection_exception
    code === '08003' || // connection_does_not_exist
    code === '08006' || // connection_failure
    code === '08001' || // sqlclient_unable_to_establish_sqlconnection
    code === '08004'    // sqlserver_rejected_establishment_of_sqlconnection
  ) {
    return true;
  }
  // pool/connection-timeout messages from node-postgres
  const msg = (err.message || '').toLowerCase();
  if (
    msg.includes('connection terminated') ||
    msg.includes('timeout exceeded when trying to connect') ||
    msg.includes('client has encountered a connection error') ||
    msg.includes('connection ended')
  ) {
    return true;
  }
  return false;
}

function jsonError(res, status, message) {
  res.status(status).type('application/json').send(JSON.stringify({ error: message }));
}

/**
 * Build the express app. `deps` is `{ db, cache, lang, maxBodyBytes }`.
 * `db` and `cache` are injected so tests can provide fakes.
 */
function createApp(deps) {
  const {
    db,
    cache,
    lang = process.env.APP_LANG || 'js',
    maxBodyBytes = parseInt(process.env.MAX_BODY_BYTES || '1048576', 10),
  } = deps;

  if (!db) throw new Error('createApp: db dependency is required');
  if (!cache) throw new Error('createApp: cache dependency is required');

  const app = express();
  app.disable('x-powered-by');

  // Health is registered before body parsing so it stays cheap.
  app.get('/health', (_req, res) => {
    res
      .status(200)
      .type('application/json')
      .send(JSON.stringify({ status: 'ok', lang }));
  });

  // Body parser scoped to the data POST route so other routes are not affected
  // by parser side effects. We use raw + manual JSON parse so we can produce
  // our exact error contract for malformed JSON and oversized payloads.
  const rawParser = express.raw({
    type: '*/*',
    limit: maxBodyBytes,
    // Important: the limit handler in express raises an error our error
    // handler converts to 413.
  });

  const base = `/api/${lang}`;

  app.get(`${base}/data`, async (_req, res) => {
    try {
      const cached = await cache.get(ALL_KEY);
      if (cached != null) {
        return res
          .status(200)
          .type('application/json')
          .send(JSON.stringify({ source: 'cache', items: cached }));
      }
    } catch (err) {
      // cache module already swallows errors, but be defensive
      // eslint-disable-next-line no-console
      console.warn('[app] cache.get unexpected error:', err.message);
    }

    let items;
    try {
      items = await db.listAll();
    } catch (err) {
      if (isDbUnavailable(err)) {
        await maybeResetDb(db);
        return jsonError(res, 503, 'database unavailable');
      }
      // eslint-disable-next-line no-console
      console.error('[app] db.listAll error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    // best-effort cache fill
    try {
      await cache.set(ALL_KEY, items);
    } catch (_) {
      /* swallow */
    }

    return res
      .status(200)
      .type('application/json')
      .send(JSON.stringify({ source: 'db', items }));
  });

  app.get(`${base}/data/:id`, async (req, res) => {
    const id = parsePositiveInt(req.params.id);
    if (id == null) {
      return jsonError(res, 400, 'invalid id');
    }

    const key = itemKey(id);
    try {
      const cached = await cache.get(key);
      if (cached != null) {
        return res
          .status(200)
          .type('application/json')
          .send(JSON.stringify({ source: 'cache', item: cached }));
      }
    } catch (err) {
      // eslint-disable-next-line no-console
      console.warn('[app] cache.get unexpected error:', err.message);
    }

    let item;
    try {
      item = await db.getById(id);
    } catch (err) {
      if (isDbUnavailable(err)) {
        await maybeResetDb(db);
        return jsonError(res, 503, 'database unavailable');
      }
      // eslint-disable-next-line no-console
      console.error('[app] db.getById error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    if (!item) {
      return jsonError(res, 404, 'not found');
    }

    try {
      await cache.set(key, item);
    } catch (_) {
      /* swallow */
    }

    return res
      .status(200)
      .type('application/json')
      .send(JSON.stringify({ source: 'db', item }));
  });

  app.post(`${base}/data`, rawParser, async (req, res) => {
    // rawParser puts a Buffer in req.body. Parse manually to control errors.
    let payload;
    try {
      const buf = req.body;
      if (!buf || buf.length === 0) {
        return jsonError(res, 400, 'content is required and must be a non-empty string');
      }
      payload = JSON.parse(buf.toString('utf8'));
    } catch (_) {
      return jsonError(res, 400, 'malformed json');
    }

    const v = validateContent(payload);
    if (!v.ok) {
      return jsonError(res, 400, v.error);
    }

    let item;
    try {
      item = await db.insert(v.content);
    } catch (err) {
      if (isDbUnavailable(err)) {
        await maybeResetDb(db);
        return jsonError(res, 503, 'database unavailable');
      }
      // eslint-disable-next-line no-console
      console.error('[app] db.insert error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    // best-effort cache invalidation
    try {
      await cache.del(ALL_KEY);
    } catch (_) {
      /* swallow */
    }

    return res
      .status(201)
      .type('application/json')
      .send(JSON.stringify({ item }));
  });

  // Express error handler — converts body parser errors to our contract.
  // eslint-disable-next-line no-unused-vars
  app.use((err, _req, res, _next) => {
    if (!err) return jsonError(res, 500, 'internal error');
    if (err.type === 'entity.too.large' || err.status === 413) {
      return jsonError(res, 413, 'payload too large');
    }
    if (err.type === 'entity.parse.failed' || err.status === 400) {
      return jsonError(res, 400, 'malformed json');
    }
    // eslint-disable-next-line no-console
    console.error('[app] unhandled error:', err.message);
    return jsonError(res, 500, 'internal error');
  });

  return app;
}

async function maybeResetDb(db) {
  if (db && typeof db.reset === 'function') {
    try {
      await db.reset();
    } catch (_) {
      /* ignore */
    }
  }
}

module.exports = {
  createApp,
  parsePositiveInt,
  validateContent,
  isDbUnavailable,
};
