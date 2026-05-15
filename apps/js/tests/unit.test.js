'use strict';

const {
  parsePositiveInt,
  validateContent,
  isDbUnavailable,
} = require('../src/app');
const { withTimeout, itemKey, ALL_KEY } = require('../src/cache');
const { serializeRow } = require('../src/db');

describe('parsePositiveInt', () => {
  test('accepts valid positive integers as strings', () => {
    expect(parsePositiveInt('1')).toBe(1);
    expect(parsePositiveInt('42')).toBe(42);
    expect(parsePositiveInt('9999999')).toBe(9999999);
  });

  test('rejects zero, negatives, and non-integers', () => {
    expect(parsePositiveInt('0')).toBeNull();
    expect(parsePositiveInt('-1')).toBeNull();
    expect(parsePositiveInt('1.5')).toBeNull();
    expect(parsePositiveInt('abc')).toBeNull();
    expect(parsePositiveInt('1a')).toBeNull();
    expect(parsePositiveInt('')).toBeNull();
    expect(parsePositiveInt('  3 ')).toBeNull();
  });

  test('rejects non-string input', () => {
    expect(parsePositiveInt(1)).toBeNull();
    expect(parsePositiveInt(null)).toBeNull();
    expect(parsePositiveInt(undefined)).toBeNull();
  });
});

describe('validateContent', () => {
  test('accepts non-empty string content', () => {
    const v = validateContent({ content: 'hello' });
    expect(v.ok).toBe(true);
    expect(v.content).toBe('hello');
  });

  test('rejects empty string', () => {
    const v = validateContent({ content: '' });
    expect(v.ok).toBe(false);
    expect(v.error).toMatch(/content is required/);
  });

  test('rejects non-string content', () => {
    expect(validateContent({ content: 123 }).ok).toBe(false);
    expect(validateContent({ content: null }).ok).toBe(false);
    expect(validateContent({ content: { x: 1 } }).ok).toBe(false);
    expect(validateContent({}).ok).toBe(false);
  });

  test('rejects non-object body', () => {
    expect(validateContent(null).ok).toBe(false);
    expect(validateContent(undefined).ok).toBe(false);
    expect(validateContent('string').ok).toBe(false);
  });
});

describe('isDbUnavailable', () => {
  test('detects ECONNREFUSED', () => {
    const e = Object.assign(new Error('boom'), { code: 'ECONNREFUSED' });
    expect(isDbUnavailable(e)).toBe(true);
  });

  test('detects pg connection failure SQLSTATE', () => {
    const e = Object.assign(new Error('boom'), { code: '08006' });
    expect(isDbUnavailable(e)).toBe(true);
  });

  test('detects connection-terminated message', () => {
    const e = new Error('Connection terminated unexpectedly');
    expect(isDbUnavailable(e)).toBe(true);
  });

  test('non-connection error returns false', () => {
    const e = Object.assign(new Error('syntax error'), { code: '42601' });
    expect(isDbUnavailable(e)).toBe(false);
  });

  test('null/undefined safe', () => {
    expect(isDbUnavailable(null)).toBe(false);
    expect(isDbUnavailable(undefined)).toBe(false);
  });
});

describe('cache key helpers', () => {
  test('itemKey', () => {
    expect(itemKey(1)).toBe('data:1');
    expect(itemKey(123)).toBe('data:123');
  });

  test('ALL_KEY constant', () => {
    expect(ALL_KEY).toBe('data:all');
  });
});

describe('withTimeout', () => {
  test('resolves when underlying promise resolves first', async () => {
    const v = await withTimeout(Promise.resolve('ok'), 50, 'fast');
    expect(v).toBe('ok');
  });

  test('rejects with CACHE_TIMEOUT when promise is slow', async () => {
    const slow = new Promise((resolve) => setTimeout(() => resolve('late'), 100));
    await expect(withTimeout(slow, 10, 'slow')).rejects.toMatchObject({
      code: 'CACHE_TIMEOUT',
    });
  });

  test('propagates underlying rejection', async () => {
    const err = new Error('underlying');
    await expect(withTimeout(Promise.reject(err), 50, 'rej')).rejects.toBe(err);
  });
});

describe('serializeRow', () => {
  test('converts Date created_at to ISO-8601 string', () => {
    const d = new Date('2024-01-02T03:04:05.000Z');
    const row = serializeRow({ id: 7, content: 'x', created_at: d });
    expect(row).toEqual({
      id: 7,
      content: 'x',
      created_at: '2024-01-02T03:04:05.000Z',
    });
  });

  test('accepts string timestamp and produces ISO-8601 string', () => {
    const row = serializeRow({
      id: 7,
      content: 'x',
      created_at: '2024-01-02T03:04:05.000Z',
    });
    expect(row.created_at).toBe('2024-01-02T03:04:05.000Z');
  });
});
