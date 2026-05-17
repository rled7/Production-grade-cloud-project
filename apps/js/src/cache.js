'use strict';

const redis = require('redis');

const ALL_KEY = 'data:all';

function itemKey(id) {
  return `data:${id}`;
}

/**
 * Race a promise against a timeout. If the timeout fires first the
 * returned promise rejects with an error tagged `code = 'CACHE_TIMEOUT'`.
 */
function withTimeout(promise, ms, label) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => {
      const err = new Error(`cache operation timed out: ${label}`);
      err.code = 'CACHE_TIMEOUT';
      reject(err);
    }, ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

function createCache(options = {}) {
  const host = options.host || process.env.REDIS_HOST || 'localhost';
  const port = parseInt(options.port || process.env.REDIS_PORT || '6379', 10);
  const ttl = parseInt(
    options.ttl || process.env.CACHE_TTL_SECONDS || '30',
    10
  );
  const timeoutMs = parseInt(
    options.timeoutMs || process.env.REDIS_TIMEOUT_MS || '200',
    10
  );
  // TLS for ElastiCache transit encryption. ElastiCache uses an AWS-managed
  // certificate signed by a public CA, so default verification is fine.
  const tls =
    options.tls !== undefined
      ? !!options.tls
      : (process.env.REDIS_TLS || 'false').toLowerCase() === 'true';

  let client = null;
  let connecting = null;
  let lastFailureAt = 0;
  const COOLDOWN_MS = 1000; // brief cooldown after failure before retrying

  async function getClient() {
    if (client && client.isOpen) return client;
    if (Date.now() - lastFailureAt < COOLDOWN_MS) {
      const err = new Error('cache unavailable (cooldown)');
      err.code = 'CACHE_UNAVAILABLE';
      throw err;
    }
    if (!connecting) {
      const c = redis.createClient({
        socket: {
          host,
          port,
          tls,
          connectTimeout: timeoutMs,
          reconnectStrategy: () => false, // disable auto-retry; we manage it
        },
      });
      c.on('error', (err) => {
        // eslint-disable-next-line no-console
        console.warn('[cache] error:', err.message);
        lastFailureAt = Date.now();
      });
      connecting = c
        .connect()
        .then(() => {
          client = c;
          return c;
        })
        .catch((err) => {
          lastFailureAt = Date.now();
          try {
            c.removeAllListeners();
          } catch (_) {
            /* ignore */
          }
          throw err;
        })
        .finally(() => {
          connecting = null;
        });
    }
    return connecting;
  }

  async function safeRun(label, fn) {
    try {
      const c = await getClient();
      return await withTimeout(fn(c), timeoutMs, label);
    } catch (err) {
      // eslint-disable-next-line no-console
      console.warn(`[cache] ${label} failed:`, err.message);
      lastFailureAt = Date.now();
      if (client) {
        const c = client;
        client = null;
        try {
          await c.disconnect();
        } catch (_) {
          /* ignore */
        }
      }
      return null;
    }
  }

  async function get(key) {
    const raw = await safeRun(`get ${key}`, (c) => c.get(key));
    if (raw == null) return null;
    try {
      return JSON.parse(raw);
    } catch (err) {
      // eslint-disable-next-line no-console
      console.warn('[cache] parse failed for key', key, err.message);
      return null;
    }
  }

  async function set(key, value) {
    const payload = JSON.stringify(value);
    await safeRun(`set ${key}`, (c) =>
      c.set(key, payload, { EX: ttl })
    );
  }

  async function del(key) {
    await safeRun(`del ${key}`, (c) => c.del(key));
  }

  async function close() {
    if (client) {
      const c = client;
      client = null;
      try {
        await c.disconnect();
      } catch (_) {
        /* ignore */
      }
    }
  }

  return {
    get,
    set,
    del,
    close,
    // exposed for tests / introspection
    _config: { host, port, ttl, timeoutMs, tls },
  };
}

module.exports = { createCache, withTimeout, itemKey, ALL_KEY };
