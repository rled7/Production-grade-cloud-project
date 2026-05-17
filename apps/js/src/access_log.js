'use strict';

const fs = require('fs');
const path = require('path');
const morgan = require('morgan');
const rfs = require('rotating-file-stream');

/**
 * Build a morgan middleware that writes one line per request to a rotating
 * file at `logPath`, plus duplicates to stdout for CloudWatch capture.
 *
 * Rotation: by size, default 10 MiB, keeping `backups` rotated copies.
 */
function buildAccessLog({ logPath, maxBytes = 10 * 1024 * 1024, backups = 5 } = {}) {
  if (!logPath) {
    return morgan('combined');
  }

  fs.mkdirSync(path.dirname(logPath), { recursive: true });
  const stream = rfs.createStream(path.basename(logPath), {
    path: path.dirname(logPath),
    size: `${maxBytes}B`,
    maxFiles: backups,
  });

  // Custom token: authenticated user's id (or "-")
  morgan.token('user', (req) => (req.user && req.user.sub) || '-');

  const fmt = ':remote-addr - :user [:date[iso]] ":method :url HTTP/:http-version" '
            + ':status :res[content-length] - :response-time ms';

  return [
    morgan(fmt, { stream }),     // file
    morgan(fmt),                  // stdout
  ];
}

module.exports = { buildAccessLog };
