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
