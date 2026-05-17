'use strict';

const express = require('express');
const bcrypt = require('bcryptjs');
const { itemKey, ALL_KEY } = require('./cache');
const auth = require('./auth');

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
  if (
    code === 'ECONNREFUSED' || code === 'ENOTFOUND' || code === 'ETIMEDOUT' ||
    code === 'ECONNRESET'  || code === 'EHOSTUNREACH' || code === 'EAI_AGAIN' ||
    code === '57P01' || code === '57P02' || code === '57P03' ||
    code === '08000' || code === '08003' || code === '08006' ||
    code === '08001' || code === '08004'
  ) return true;
  const msg = (err.message || '').toLowerCase();
  return (
    msg.includes('connection terminated') ||
    msg.includes('timeout exceeded when trying to connect') ||
    msg.includes('client has encountered a connection error') ||
    msg.includes('connection ended')
  );
}

function jsonError(res, status, message) {
  res.status(status).type('application/json').send(JSON.stringify({ error: message }));
}

/**
 * Build the express app.
 * deps = {
 *   db, cache,
 *   lang, maxBodyBytes,
 *   apiKey,                 // X-API-Key (defense-in-depth gate). "" disables.
 *   jwtSecret,              // HS256 secret for session cookie. "" disables.
 *   cookieSecure,           // boolean — Secure attribute on session cookie.
 *   accessLog,              // optional pre-built morgan middleware (array or single)
 * }
 */
function createApp(deps) {
  const {
    db,
    cache,
    lang = process.env.APP_LANG || 'js',
    maxBodyBytes = parseInt(process.env.MAX_BODY_BYTES || '1048576', 10),
    apiKey = process.env.API_KEY || '',
    apiKeyNext = process.env.API_KEY_NEXT || '',
    jwtSecret = process.env.JWT_SECRET || '',
    cookieSecure = (process.env.COOKIE_SECURE || 'true').toLowerCase() !== 'false',
    accessLog,
  } = deps;

  if (!db) throw new Error('createApp: db dependency is required');
  if (!cache) throw new Error('createApp: cache dependency is required');

  const app = express();
  app.disable('x-powered-by');

  // Access log first (so /health is also logged).
  if (accessLog) {
    if (Array.isArray(accessLog)) accessLog.forEach((m) => app.use(m));
    else app.use(accessLog);
  }

  // /health is unauthenticated — ALB target-group health check.
  app.get('/health', (_req, res) => {
    res.status(200).type('application/json').send(JSON.stringify({ status: 'ok', lang }));
  });

  // ----- API-key gate (every /api/<lang>/* route) -----
  // During rotation, apiKeyNext is also accepted alongside apiKey.
  if (apiKey || apiKeyNext) {
    app.use((req, res, next) => {
      if (!req.path.startsWith(`/api/${lang}`)) return next();
      const presented = req.header('x-api-key');
      if (!presented) return jsonError(res, 401, 'missing api key');
      const ok =
        (apiKey && presented === apiKey) ||
        (apiKeyNext && presented === apiKeyNext);
      if (!ok) return jsonError(res, 401, 'invalid api key');
      next();
    });
  }

  // ----- JWT verification middleware: populates req.user when valid -----
  if (jwtSecret) {
    app.use((req, _res, next) => {
      const token = auth.extractToken(req);
      if (token) {
        const claims = auth.verifySession(token, jwtSecret);
        if (claims) req.user = claims;
      }
      next();
    });
  }

  function requireAuth(req, res, next) {
    if (!jwtSecret) return next(); // disabled (tests)
    if (!req.user) return jsonError(res, 401, 'authentication required');
    next();
  }

  function requireRole(...roles) {
    return (req, res, next) => {
      if (!jwtSecret) return next();
      if (!req.user) return jsonError(res, 401, 'authentication required');
      if (!auth.hasRole(req.user, ...roles)) return jsonError(res, 403, 'forbidden');
      next();
    };
  }

  const base = `/api/${lang}`;

  // ----- Auth routes -----
  app.post(`${base}/auth/login`, express.json({ limit: 8192 }), async (req, res) => {
    const body = req.body || {};
    const email = typeof body.email === 'string' ? body.email.trim() : '';
    const password = typeof body.password === 'string' ? body.password : '';
    if (!email || !password) {
      return jsonError(res, 400, 'email and password are required');
    }

    let user;
    try {
      user = await db.findUserByEmail(email);
    } catch (err) {
      if (isDbUnavailable(err)) return jsonError(res, 503, 'database unavailable');
      console.error('[app] findUserByEmail error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    if (!user) return jsonError(res, 401, 'invalid credentials');

    let ok = false;
    try { ok = await bcrypt.compare(password, user.password_hash); } catch (_) { ok = false; }
    if (!ok) return jsonError(res, 401, 'invalid credentials');

    if (!jwtSecret) return jsonError(res, 500, 'auth not configured');

    const token = auth.signSession(
      { sub: String(user.id), email: user.email, roles: user.roles },
      jwtSecret
    );
    res.setHeader('Set-Cookie', auth.buildSetCookie(token, { secure: cookieSecure }));
    res.status(200).type('application/json').send(JSON.stringify({
      user: { id: user.id, email: user.email, roles: user.roles },
    }));
  });

  app.post(`${base}/auth/logout`, (_req, res) => {
    res.setHeader('Set-Cookie', auth.buildClearCookie({ secure: cookieSecure }));
    res.status(204).end();
  });

  app.get(`${base}/auth/me`, requireAuth, (req, res) => {
    res.status(200).type('application/json').send(JSON.stringify({
      user: { id: Number(req.user.sub), email: req.user.email, roles: req.user.roles },
    }));
  });

  // ----- Data routes (protected) -----
  app.get(`${base}/data`, requireAuth, async (_req, res) => {
    try {
      const cached = await cache.get(ALL_KEY);
      if (cached != null) {
        return res.status(200).type('application/json')
          .send(JSON.stringify({ source: 'cache', items: cached }));
      }
    } catch (err) {
      console.warn('[app] cache.get unexpected error:', err.message);
    }

    let items;
    try {
      items = await db.listAll();
    } catch (err) {
      if (isDbUnavailable(err)) { await maybeResetDb(db); return jsonError(res, 503, 'database unavailable'); }
      console.error('[app] db.listAll error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    try { await cache.set(ALL_KEY, items); } catch (_) { /* swallow */ }
    return res.status(200).type('application/json')
      .send(JSON.stringify({ source: 'db', items }));
  });

  app.get(`${base}/data/:id`, requireAuth, async (req, res) => {
    const id = parsePositiveInt(req.params.id);
    if (id == null) return jsonError(res, 400, 'invalid id');

    const key = itemKey(id);
    try {
      const cached = await cache.get(key);
      if (cached != null) {
        return res.status(200).type('application/json')
          .send(JSON.stringify({ source: 'cache', item: cached }));
      }
    } catch (err) {
      console.warn('[app] cache.get unexpected error:', err.message);
    }

    let item;
    try {
      item = await db.getById(id);
    } catch (err) {
      if (isDbUnavailable(err)) { await maybeResetDb(db); return jsonError(res, 503, 'database unavailable'); }
      console.error('[app] db.getById error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    if (!item) return jsonError(res, 404, 'not found');
    try { await cache.set(key, item); } catch (_) { /* swallow */ }

    return res.status(200).type('application/json')
      .send(JSON.stringify({ source: 'db', item }));
  });

  const rawParser = express.raw({ type: '*/*', limit: maxBodyBytes });

  app.post(`${base}/data`, requireRole('writer', 'admin'), rawParser, async (req, res) => {
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
    if (!v.ok) return jsonError(res, 400, v.error);

    let item;
    try {
      item = await db.insert(v.content);
    } catch (err) {
      if (isDbUnavailable(err)) { await maybeResetDb(db); return jsonError(res, 503, 'database unavailable'); }
      console.error('[app] db.insert error:', err.message);
      return jsonError(res, 503, 'database unavailable');
    }

    try { await cache.del(ALL_KEY); } catch (_) { /* swallow */ }
    return res.status(201).type('application/json').send(JSON.stringify({ item }));
  });

  app.use((err, _req, res, _next) => {
    if (!err) return jsonError(res, 500, 'internal error');
    if (err.type === 'entity.too.large' || err.status === 413) return jsonError(res, 413, 'payload too large');
    if (err.type === 'entity.parse.failed' || err.status === 400) return jsonError(res, 400, 'malformed json');
    console.error('[app] unhandled error:', err.message);
    return jsonError(res, 500, 'internal error');
  });

  return app;
}

async function maybeResetDb(db) {
  if (db && typeof db.reset === 'function') {
    try { await db.reset(); } catch (_) { /* ignore */ }
  }
}

module.exports = {
  createApp,
  parsePositiveInt,
  validateContent,
  isDbUnavailable,
};
