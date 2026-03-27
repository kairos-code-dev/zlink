'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src');

test('dedicated pub option constants stay available on SocketOption', () => {
  const ctx = new zlink.Context();
  const xpub = new zlink.XPubSocket(ctx);

  const verbose = Buffer.alloc(4);
  verbose.writeInt32LE(1, 0);

  assert.doesNotThrow(() => xpub.setOption(zlink.SocketOption.XPUB_VERBOSE, verbose));

  xpub.close();
  ctx.close();
});
