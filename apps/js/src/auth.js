'use strict';

const jwt = require('jsonwebtoken');
const cookie = require('cookie');

const COOKIE_NAME = 'session';
const TOKEN_TTL_SECONDS = 60 * 60; // 1 hour

function signSession(payload, secret) {
  return jwt.sign(payload, secret, { algorithm: 'HS256', expiresIn: TOKEN_TTL_SECONDS });
}

function verifySession(token, secret, secretNext) {
  try {
    return jwt.verify(token, secret, { algorithms: ['HS256'] });
  } catch (_) {
    if (!secretNext) return null;
    try {
      return jwt.verify(token, secretNext, { algorithms: ['HS256'] });
    } catch (_) {
      return null;
    }
  }
}

function extractToken(req) {
  const raw = req.headers && req.headers.cookie;
  if (!raw) return null;
  const parsed = cookie.parse(raw);
  return parsed[COOKIE_NAME] || null;
}

function buildSetCookie(token, { secure = true, maxAgeSeconds = TOKEN_TTL_SECONDS } = {}) {
  return cookie.serialize(COOKIE_NAME, token, {
    httpOnly: true,
    sameSite: 'strict',
    secure,
    path: '/',
    maxAge: maxAgeSeconds,
  });
}

function buildClearCookie({ secure = true } = {}) {
  return cookie.serialize(COOKIE_NAME, '', {
    httpOnly: true,
    sameSite: 'strict',
    secure,
    path: '/',
    maxAge: 0,
  });
}

function hasRole(user, ...required) {
  if (!user || !Array.isArray(user.roles)) return false;
  const set = new Set(user.roles);
  return required.some((r) => set.has(r));
}

module.exports = {
  COOKIE_NAME,
  TOKEN_TTL_SECONDS,
  signSession,
  verifySession,
  extractToken,
  buildSetCookie,
  buildClearCookie,
  hasRole,
};
