'use strict';

const request = require('supertest');
const { createApp } = require('../src/app');

/**
 * Build a fake DB with controllable behavior. Each call records into `calls`
 * so individual tests can assert ordering.
 */
function makeFakeDb(initialRows = []) {
  const rows = initialRows.slice();
  let nextId = rows.reduce((m, r) => Math.max(m, r.id), 0) + 1;
  const calls = [];
  const state = { failMode: null };

  function maybeFail(op) {
    if (state.failMode === 'unavailable') {
      const e = new Error('connect ECONNREFUSED 127.0.0.1:5432');
      e.code = 'ECONNREFUSED';
      throw e;
    }
    if (state.failMode === 'syntax') {
      const e = new Error('syntax error at or near "FOO"');
      e.code = '42601';
      throw e;
    }
  }

  return {
    listAll: jest.fn(async () => {
      calls.push(['listAll']);
      maybeFail('listAll');
      return rows.slice().sort((a, b) => (a.created_at < b.created_at ? 1 : -1));
    }),
    getById: jest.fn(async (id) => {
      calls.push(['getById', id]);
      maybeFail('getById');
      return rows.find((r) => r.id === id) || null;
    }),
    insert: jest.fn(async (content) => {
      calls.push(['insert', content]);
      maybeFail('insert');
      const item = {
        id: nextId++,
        content,
        created_at: new Date().toISOString(),
      };
      rows.push(item);
      return item;
    }),
    ensureSchema: jest.fn(async () => {}),
    reset: jest.fn(async () => {}),
    close: jest.fn(async () => {}),
    _calls: calls,
    _state: state,
    _rows: rows,
  };
}

/**
 * Build a fake cache. By default it acts as an always-miss cache. Tests can
 * flip into different failure modes via `_state`.
 */
function makeFakeCache() {
  const store = new Map();
  const state = { mode: 'normal' }; // 'normal' | 'timeout' | 'throw' | 'returns-null'
  const calls = [];

  function maybeFail(label) {
    if (state.mode === 'timeout') {
      // Simulate the timeout race producing a CACHE_TIMEOUT failure that the
      // cache wrapper would normally swallow and return null for. The
      // production cache module returns null on failure — we do the same here
      // because the app contract is "treat null as cache miss".
      return Promise.resolve(null);
    }
    if (state.mode === 'throw') {
      const e = new Error('cache exploded');
      return Promise.reject(e);
    }
    return null;
  }

  return {
    get: jest.fn(async (key) => {
      calls.push(['get', key]);
      const f = maybeFail();
      if (f && typeof f.then === 'function') return f;
      if (state.mode === 'timeout' || state.mode === 'returns-null') return null;
      return store.has(key) ? store.get(key) : null;
    }),
    set: jest.fn(async (key, value) => {
      calls.push(['set', key, value]);
      if (state.mode === 'timeout' || state.mode === 'throw') return;
      store.set(key, value);
    }),
    del: jest.fn(async (key) => {
      calls.push(['del', key]);
      if (state.mode === 'timeout' || state.mode === 'throw') return;
      store.delete(key);
    }),
    close: jest.fn(async () => {}),
    _state: state,
    _store: store,
    _calls: calls,
  };
}

function buildApp(overrides = {}) {
  const db = overrides.db || makeFakeDb(overrides.initialRows || []);
  const cache = overrides.cache || makeFakeCache();
  const app = createApp({
    db,
    cache,
    lang: overrides.lang || 'js',
    maxBodyBytes: overrides.maxBodyBytes || 1024, // small for size tests
    apiKey:     overrides.apiKey     !== undefined ? overrides.apiKey     : '',
    apiKeyNext: overrides.apiKeyNext !== undefined ? overrides.apiKeyNext : '',
    jwtSecret:  overrides.jwtSecret  !== undefined ? overrides.jwtSecret  : '',
    cookieSecure: overrides.cookieSecure !== undefined ? overrides.cookieSecure : false,
  });
  return { app, db, cache };
}

describe('GET /health', () => {
  test('returns 200 ok with lang', async () => {
    const { app } = buildApp();
    const res = await request(app).get('/health');
    expect(res.status).toBe(200);
    expect(res.headers['content-type']).toMatch(/application\/json/);
    expect(res.body).toEqual({ status: 'ok', lang: 'js' });
  });

  test('lang reflects override', async () => {
    const { app } = buildApp({ lang: 'rust' });
    const res = await request(app).get('/health');
    expect(res.body.lang).toBe('rust');
  });
});

describe('GET /api/js/data', () => {
  test('serves from db on cache miss and fills cache', async () => {
    const seed = [
      { id: 1, content: 'a', created_at: '2024-01-01T00:00:00.000Z' },
      { id: 2, content: 'b', created_at: '2024-01-02T00:00:00.000Z' },
    ];
    const { app, db, cache } = buildApp({ initialRows: seed });
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('db');
    expect(res.body.items.map((x) => x.id)).toEqual([2, 1]);
    expect(db.listAll).toHaveBeenCalledTimes(1);
    expect(cache.set).toHaveBeenCalledWith('data:all', expect.any(Array));
  });

  test('serves from cache when key is set', async () => {
    const { app, db, cache } = buildApp();
    cache._store.set('data:all', [
      { id: 99, content: 'cached', created_at: '2024-05-01T00:00:00.000Z' },
    ]);
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('cache');
    expect(res.body.items[0].id).toBe(99);
    expect(db.listAll).not.toHaveBeenCalled();
  });

  test('falls back to db with 200 when redis times out (returns null)', async () => {
    const { app, db, cache } = buildApp({
      initialRows: [
        { id: 1, content: 'x', created_at: '2024-01-01T00:00:00.000Z' },
      ],
    });
    cache._state.mode = 'timeout';
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(200); // CRITICAL: not 500
    expect(res.body.source).toBe('db');
    expect(res.body.items).toHaveLength(1);
    expect(db.listAll).toHaveBeenCalled();
  });

  test('falls back to db with 200 when redis throws unexpectedly', async () => {
    const { app, db, cache } = buildApp({
      initialRows: [
        { id: 1, content: 'x', created_at: '2024-01-01T00:00:00.000Z' },
      ],
    });
    cache._state.mode = 'throw';
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('db');
    expect(db.listAll).toHaveBeenCalled();
  });

  test('returns 503 when db is unavailable', async () => {
    const { app, db } = buildApp();
    db._state.failMode = 'unavailable';
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(503);
    expect(res.body).toEqual({ error: 'database unavailable' });
  });
});

describe('GET /api/js/data/:id', () => {
  test('400 on non-numeric id', async () => {
    const { app } = buildApp();
    const res = await request(app).get('/api/js/data/abc');
    expect(res.status).toBe(400);
    expect(res.body).toEqual({ error: 'invalid id' });
  });

  test('400 on zero id', async () => {
    const { app } = buildApp();
    const res = await request(app).get('/api/js/data/0');
    expect(res.status).toBe(400);
  });

  test('404 when id does not exist', async () => {
    const { app } = buildApp();
    const res = await request(app).get('/api/js/data/123');
    expect(res.status).toBe(404);
    expect(res.body).toEqual({ error: 'not found' });
  });

  test('200 from db then cached', async () => {
    const seed = [
      { id: 5, content: 'x', created_at: '2024-01-01T00:00:00.000Z' },
    ];
    const { app, db, cache } = buildApp({ initialRows: seed });
    const res = await request(app).get('/api/js/data/5');
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('db');
    expect(res.body.item).toEqual(seed[0]);
    expect(db.getById).toHaveBeenCalledWith(5);
    expect(cache.set).toHaveBeenCalledWith('data:5', seed[0]);
  });

  test('200 from cache when present', async () => {
    const { app, db, cache } = buildApp();
    const item = { id: 7, content: 'cached', created_at: '2024-01-01T00:00:00.000Z' };
    cache._store.set('data:7', item);
    const res = await request(app).get('/api/js/data/7');
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('cache');
    expect(res.body.item).toEqual(item);
    expect(db.getById).not.toHaveBeenCalled();
  });

  test('falls back to db with 200 when redis times out on single-item read', async () => {
    const { app, cache } = buildApp({
      initialRows: [
        { id: 11, content: 'k', created_at: '2024-01-01T00:00:00.000Z' },
      ],
    });
    cache._state.mode = 'timeout';
    const res = await request(app).get('/api/js/data/11');
    expect(res.status).toBe(200); // CRITICAL: not 500
    expect(res.body.source).toBe('db');
    expect(res.body.item.id).toBe(11);
  });
});

describe('POST /api/js/data', () => {
  test('201 inserts item and invalidates data:all', async () => {
    const { app, db, cache } = buildApp();
    cache._store.set('data:all', [{ id: 1, content: 'old', created_at: '2024-01-01T00:00:00.000Z' }]);
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: 'hello' }));
    expect(res.status).toBe(201);
    expect(res.body.item).toMatchObject({ content: 'hello' });
    expect(typeof res.body.item.id).toBe('number');
    expect(typeof res.body.item.created_at).toBe('string');
    expect(db.insert).toHaveBeenCalledWith('hello');
    expect(cache.del).toHaveBeenCalledWith('data:all');
    expect(cache._store.has('data:all')).toBe(false);
  });

  test('400 on malformed JSON', async () => {
    const { app } = buildApp();
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send('{not: valid json');
    expect(res.status).toBe(400);
    expect(res.body).toEqual({ error: 'malformed json' });
  });

  test('400 on missing content', async () => {
    const { app } = buildApp();
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({}));
    expect(res.status).toBe(400);
    expect(res.body).toEqual({
      error: 'content is required and must be a non-empty string',
    });
  });

  test('400 on empty content', async () => {
    const { app } = buildApp();
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: '' }));
    expect(res.status).toBe(400);
  });

  test('400 on non-string content', async () => {
    const { app } = buildApp();
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: 42 }));
    expect(res.status).toBe(400);
  });

  test('413 when body exceeds MAX_BODY_BYTES', async () => {
    const { app } = buildApp({ maxBodyBytes: 32 });
    const big = 'x'.repeat(1000);
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: big }));
    expect(res.status).toBe(413);
    expect(res.body).toEqual({ error: 'payload too large' });
  });

  test('503 when db unavailable on insert', async () => {
    const { app, db } = buildApp();
    db._state.failMode = 'unavailable';
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: 'hi' }));
    expect(res.status).toBe(503);
    expect(res.body).toEqual({ error: 'database unavailable' });
  });

  test('insert still succeeds (201) when cache invalidation fails', async () => {
    const { app, cache } = buildApp();
    cache._state.mode = 'throw';
    const res = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: 'survive cache failure' }));
    expect(res.status).toBe(201);
    expect(res.body.item.content).toBe('survive cache failure');
  });
});

describe('content-type discipline', () => {
  test('all error responses are application/json', async () => {
    const { app } = buildApp();
    for (const r of [
      await request(app).get('/api/js/data/abc'),
      await request(app).get('/api/js/data/999'),
      await request(app)
        .post('/api/js/data')
        .set('Content-Type', 'application/json')
        .send('{bad'),
      await request(app)
        .post('/api/js/data')
        .set('Content-Type', 'application/json')
        .send('{}'),
    ]) {
      expect(r.headers['content-type']).toMatch(/application\/json/);
    }
  });
});

describe('API key auth', () => {
  const KEY = 'test-key-abc123';

  test('GET /health works without an API key (ALB health check stays public)', async () => {
    const { app } = buildApp({ apiKey: KEY });
    const res = await request(app).get('/health');
    expect(res.status).toBe(200);
    expect(res.body).toEqual({ status: 'ok', lang: 'js' });
  });

  test('data routes return 401 with no key', async () => {
    const { app } = buildApp({ apiKey: KEY });
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(401);
    expect(res.body).toEqual({ error: 'missing api key' });
  });

  test('data routes return 401 with a wrong key', async () => {
    const { app } = buildApp({ apiKey: KEY });
    const res = await request(app).get('/api/js/data').set('X-API-Key', 'nope');
    expect(res.status).toBe(401);
    expect(res.body).toEqual({ error: 'invalid api key' });
  });

  test('data routes return 200 with the right key', async () => {
    const { app } = buildApp({ apiKey: KEY, initialRows: [
      { id: 1, content: 'x', created_at: '2024-01-01T00:00:00Z' },
    ]});
    const res = await request(app).get('/api/js/data').set('X-API-Key', KEY);
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('db');
    expect(res.body.items.length).toBe(1);
  });

  test('POST /api/js/data also requires the key', async () => {
    const { app } = buildApp({ apiKey: KEY });
    const r1 = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send('{"content":"hi"}');
    expect(r1.status).toBe(401);

    const r2 = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .set('X-API-Key', KEY)
      .send('{"content":"hi"}');
    expect(r2.status).toBe(201);
  });

  test('an empty apiKey disables auth entirely', async () => {
    const { app } = buildApp({ apiKey: '' });
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(200);
  });
});

// ---------- JWT cookie auth ----------

const bcryptjs = require('bcryptjs');
const jsonwebtoken = require('jsonwebtoken');

const JWT_SECRET = 'jwt-test-secret-aaaa';

function userRow(overrides = {}) {
  const pw = overrides.password || 'pa$$w0rd';
  return {
    id: overrides.id || 1,
    email: overrides.email || 'alice@example.com',
    password_hash: bcryptjs.hashSync(pw, 4),
    roles: overrides.roles || ['reader'],
    _password: pw,
  };
}

function makeAuthDb(users = []) {
  const base = makeFakeDb();
  base.findUserByEmail = jest.fn(async (email) => {
    return users.find((u) => u.email.toLowerCase() === email.toLowerCase()) || null;
  });
  return base;
}

function authedApp(users) {
  const db = makeAuthDb(users);
  const cache = makeFakeCache();
  const app = createApp({
    db, cache, lang: 'js',
    apiKey: '',                   // skip api-key for auth-only tests
    jwtSecret: JWT_SECRET,
    cookieSecure: false,
  });
  return { app, db, cache };
}

describe('POST /api/js/auth/login', () => {
  test('200 + session cookie on correct credentials', async () => {
    const u = userRow({ roles: ['reader'] });
    const { app } = authedApp([u]);
    const res = await request(app).post('/api/js/auth/login')
      .send({ email: u.email, password: u._password });
    expect(res.status).toBe(200);
    expect(res.body.user.email).toBe(u.email);
    const cookie = res.headers['set-cookie'][0];
    expect(cookie).toMatch(/^session=/);
    expect(cookie).toMatch(/HttpOnly/);
    expect(cookie).toMatch(/SameSite=Strict/);
  });

  test('401 on wrong password', async () => {
    const u = userRow();
    const { app } = authedApp([u]);
    const res = await request(app).post('/api/js/auth/login')
      .send({ email: u.email, password: 'nope' });
    expect(res.status).toBe(401);
    expect(res.body).toEqual({ error: 'invalid credentials' });
  });

  test('401 on unknown email', async () => {
    const { app } = authedApp([userRow()]);
    const res = await request(app).post('/api/js/auth/login')
      .send({ email: 'ghost@x.com', password: 'whatever' });
    expect(res.status).toBe(401);
  });

  test('400 on missing fields', async () => {
    const { app } = authedApp([userRow()]);
    const r1 = await request(app).post('/api/js/auth/login').send({});
    expect(r1.status).toBe(400);
    const r2 = await request(app).post('/api/js/auth/login').send({ email: 'a@b.c' });
    expect(r2.status).toBe(400);
  });
});

describe('GET /api/js/auth/me', () => {
  test('401 with no cookie', async () => {
    const { app } = authedApp([userRow()]);
    const res = await request(app).get('/api/js/auth/me');
    expect(res.status).toBe(401);
  });

  test('returns user when valid cookie present', async () => {
    const u = userRow({ id: 42, email: 'alice@example.com', roles: ['admin'] });
    const { app } = authedApp([u]);
    const token = jsonwebtoken.sign(
      { sub: '42', email: u.email, roles: u.roles },
      JWT_SECRET,
      { algorithm: 'HS256', expiresIn: 60 }
    );
    const res = await request(app).get('/api/js/auth/me')
      .set('Cookie', `session=${token}`);
    expect(res.status).toBe(200);
    expect(res.body.user).toEqual({ id: 42, email: u.email, roles: u.roles });
  });

  test('401 with an expired token', async () => {
    const u = userRow();
    const { app } = authedApp([u]);
    const token = jsonwebtoken.sign(
      { sub: '1', email: u.email, roles: u.roles },
      JWT_SECRET,
      { algorithm: 'HS256', expiresIn: -10 }
    );
    const res = await request(app).get('/api/js/auth/me')
      .set('Cookie', `session=${token}`);
    expect(res.status).toBe(401);
  });

  test('401 with a tampered token', async () => {
    const { app } = authedApp([userRow()]);
    const bad = jsonwebtoken.sign({ sub: '1' }, 'wrong-secret', { algorithm: 'HS256', expiresIn: 60 });
    const res = await request(app).get('/api/js/auth/me')
      .set('Cookie', `session=${bad}`);
    expect(res.status).toBe(401);
  });
});

describe('protected data routes', () => {
  test('GET /api/js/data → 401 with no cookie', async () => {
    const { app } = authedApp([userRow()]);
    const res = await request(app).get('/api/js/data');
    expect(res.status).toBe(401);
  });

  test('GET /api/js/data → 200 with a valid cookie', async () => {
    const u = userRow({ roles: ['reader'] });
    const { app } = authedApp([u]);
    // login to acquire cookie
    const login = await request(app).post('/api/js/auth/login')
      .send({ email: u.email, password: u._password });
    const session = login.headers['set-cookie'][0].split(';')[0];
    const res = await request(app).get('/api/js/data').set('Cookie', session);
    expect(res.status).toBe(200);
    expect(res.body.source).toBe('db');
  });

  test('POST /api/js/data → 403 when role lacks writer/admin', async () => {
    const u = userRow({ roles: ['reader'] });
    const { app } = authedApp([u]);
    const login = await request(app).post('/api/js/auth/login')
      .send({ email: u.email, password: u._password });
    const session = login.headers['set-cookie'][0].split(';')[0];
    const res = await request(app).post('/api/js/data')
      .set('Cookie', session)
      .set('Content-Type', 'application/json')
      .send('{"content":"hi"}');
    expect(res.status).toBe(403);
    expect(res.body).toEqual({ error: 'forbidden' });
  });

  test('POST /api/js/data → 201 when role is writer', async () => {
    const u = userRow({ roles: ['writer'] });
    const { app } = authedApp([u]);
    const login = await request(app).post('/api/js/auth/login')
      .send({ email: u.email, password: u._password });
    const session = login.headers['set-cookie'][0].split(';')[0];
    const res = await request(app).post('/api/js/data')
      .set('Cookie', session)
      .set('Content-Type', 'application/json')
      .send('{"content":"hi"}');
    expect(res.status).toBe(201);
  });
});

describe('POST /api/js/auth/logout', () => {
  test('clears session cookie and returns 204', async () => {
    const { app } = authedApp([userRow()]);
    const res = await request(app).post('/api/js/auth/logout');
    expect(res.status).toBe(204);
    const cookie = res.headers['set-cookie'][0];
    expect(cookie).toMatch(/^session=;/);
    expect(cookie).toMatch(/Max-Age=0/);
  });
});

// ---------- Edge cases ----------

describe('content edge cases', () => {
  test('unicode roundtrips through POST + GET', async () => {
    const { app } = buildApp({ maxBodyBytes: 65536 });
    const payload = { content: 'hello 🌍 — résumé naïve façade Ω' };
    const r1 = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify(payload));
    expect(r1.status).toBe(201);
    expect(r1.body.item.content).toBe(payload.content);
    const id = r1.body.item.id;
    const r2 = await request(app).get(`/api/js/data/${id}`);
    expect(r2.status).toBe(200);
    expect(r2.body.item.content).toBe(payload.content);
  });

  test('content with embedded quotes / backslashes survives roundtrip', async () => {
    const { app } = buildApp();
    const value = 'with "quotes" and \\backslash and \nnewline';
    const r = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(JSON.stringify({ content: value }));
    expect(r.status).toBe(201);
    expect(r.body.item.content).toBe(value);
  });

  test('body exactly at MAX_BODY_BYTES is accepted; +1 is 413', async () => {
    const cap = 256;
    const { app } = buildApp({ maxBodyBytes: cap });
    const overhead = '{"content":""}'.length;        // 14
    const exactBody = JSON.stringify({ content: 'x'.repeat(cap - overhead) });
    expect(exactBody.length).toBe(cap);
    const ok = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(exactBody);
    expect(ok.status).toBe(201);

    const tooBig = JSON.stringify({ content: 'x'.repeat(cap - overhead + 1) });
    const big = await request(app)
      .post('/api/js/data')
      .set('Content-Type', 'application/json')
      .send(tooBig);
    expect(big.status).toBe(413);
  });
});

describe('method discipline', () => {
  test('DELETE /api/js/data returns 4xx (express default 404)', async () => {
    const { app } = buildApp();
    const r = await request(app).delete('/api/js/data');
    expect(r.status).toBeGreaterThanOrEqual(400);
    expect(r.status).toBeLessThan(500);
  });
});

describe('auth edge cases', () => {
  const KEY = 'k';
  const SECRET = 'jwt-secret-aaaa';

  test('login with non-string email/password returns 400', async () => {
    const { app } = authedApp([userRow()]);
    const r1 = await request(app).post('/api/js/auth/login').send({ email: 1, password: 'p' });
    expect(r1.status).toBe(400);
    const r2 = await request(app).post('/api/js/auth/login').send({ email: 'a@b', password: null });
    expect(r2.status).toBe(400);
  });

  test('malformed JWT (one dot, four dots, garbage) → 401', async () => {
    const { app } = authedApp([userRow()]);
    for (const bad of ['only.one', 'a.b.c.d', 'garbage', '..']) {
      const r = await request(app).get('/api/js/auth/me').set('Cookie', `session=${bad}`);
      expect(r.status).toBe(401);
    }
  });

  test('cookie value with surrounding whitespace is parsed correctly', async () => {
    const u = userRow({ roles: ['reader'] });
    const { app } = authedApp([u]);
    const token = jsonwebtoken.sign({ sub: '1', email: u.email, roles: u.roles }, JWT_SECRET, {
      algorithm: 'HS256', expiresIn: 60,
    });
    const r = await request(app).get('/api/js/auth/me').set('Cookie', `  other=foo  ; session=${token}  `);
    expect(r.status).toBe(200);
  });

  test('X-API-Key header is case-insensitive (HTTP semantics)', async () => {
    const { app } = buildApp({ apiKey: KEY });
    const r1 = await request(app).get('/api/js/data').set('x-api-key', KEY);
    const r2 = await request(app).get('/api/js/data').set('X-API-KEY', KEY);
    expect(r1.status).not.toBe(401);
    expect(r2.status).not.toBe(401);
  });
});

describe('routing edge cases', () => {
  test('unknown /api/<other-lang> path → 404', async () => {
    const { app } = buildApp();
    const r = await request(app).get('/api/rust/data');
    expect(r.status).toBe(404);
  });

  test('path with trailing slash should not crash', async () => {
    const { app } = buildApp();
    // express by default does not match trailing slash — accept 404 or 200/401
    const r = await request(app).get('/api/js/data/');
    expect(r.status).toBeLessThan(500);
  });
});

describe('API-key rotation (api_key + api_key_next)', () => {
  const OLD = 'old-key';
  const NEW = 'new-key';

  test('both keys are accepted while both are configured', async () => {
    const { app } = buildApp({ apiKey: OLD, apiKeyNext: NEW });
    expect((await request(app).get('/api/js/data').set('X-API-Key', OLD)).status).toBe(200);
    expect((await request(app).get('/api/js/data').set('X-API-Key', NEW)).status).toBe(200);
    expect((await request(app).get('/api/js/data').set('X-API-Key', 'nope')).status).toBe(401);
  });

  test('after swap, only the new key is accepted', async () => {
    const { app } = buildApp({ apiKey: NEW, apiKeyNext: '' });
    expect((await request(app).get('/api/js/data').set('X-API-Key', NEW)).status).toBe(200);
    expect((await request(app).get('/api/js/data').set('X-API-Key', OLD)).status).toBe(401);
  });

  test('empty apiKey but set apiKeyNext still enforces auth', async () => {
    const { app } = buildApp({ apiKey: '', apiKeyNext: NEW });
    expect((await request(app).get('/api/js/data').set('X-API-Key', NEW)).status).toBe(200);
    expect((await request(app).get('/api/js/data')).status).toBe(401);
  });
});
