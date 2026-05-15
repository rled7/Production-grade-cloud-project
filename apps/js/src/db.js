'use strict';

const { Pool } = require('pg');

/**
 * Create a Postgres-backed data layer. The pool is created lazily on
 * first use so that a temporarily-down DB does not crash startup.
 */
function createDb(options = {}) {
  const cfg = {
    host: options.host || process.env.DB_HOST || 'localhost',
    port: parseInt(options.port || process.env.DB_PORT || '5432', 10),
    database: options.database || process.env.DB_NAME || 'appdb',
    user: options.user || process.env.DB_USER || 'app',
    password: options.password || process.env.DB_PASSWORD || 'app',
    max: 10,
    idleTimeoutMillis: 30000,
    connectionTimeoutMillis: 2000,
  };

  let pool = null;

  function getPool() {
    if (!pool) {
      pool = new Pool(cfg);
      // Swallow background errors so the process never dies because of a
      // pool-level error event from an idle connection.
      pool.on('error', (err) => {
        // eslint-disable-next-line no-console
        console.warn('[db] pool error:', err.message);
      });
    }
    return pool;
  }

  async function query(text, params) {
    const p = getPool();
    return p.query(text, params);
  }

  async function ensureSchema() {
    await query(
      'CREATE TABLE IF NOT EXISTS data (' +
        'id SERIAL PRIMARY KEY, ' +
        'content TEXT NOT NULL, ' +
        'created_at TIMESTAMPTZ NOT NULL DEFAULT NOW())'
    );
  }

  async function listAll() {
    const res = await query(
      'SELECT id, content, created_at FROM data ORDER BY created_at DESC'
    );
    return res.rows.map(serializeRow);
  }

  async function getById(id) {
    const res = await query(
      'SELECT id, content, created_at FROM data WHERE id = $1',
      [id]
    );
    if (res.rows.length === 0) return null;
    return serializeRow(res.rows[0]);
  }

  async function insert(content) {
    const res = await query(
      'INSERT INTO data (content) VALUES ($1) RETURNING id, content, created_at',
      [content]
    );
    return serializeRow(res.rows[0]);
  }

  async function close() {
    if (pool) {
      const p = pool;
      pool = null;
      await p.end();
    }
  }

  // Allow callers to reset the pool after a failure so the next request
  // attempts a fresh connection.
  async function reset() {
    if (pool) {
      const p = pool;
      pool = null;
      try {
        await p.end();
      } catch (_) {
        /* ignore */
      }
    }
  }

  return {
    query,
    ensureSchema,
    listAll,
    getById,
    insert,
    close,
    reset,
  };
}

function serializeRow(row) {
  return {
    id: row.id,
    content: row.content,
    created_at:
      row.created_at instanceof Date
        ? row.created_at.toISOString()
        : new Date(row.created_at).toISOString(),
  };
}

module.exports = { createDb, serializeRow };
