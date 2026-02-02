'use strict';

const test = require('node:test');
const assert = require('node:assert');
const {
  zlink,
  transports,
  endpointFor,
  tryTransport,
  recvWithTimeout,
  sendWithRetry,
  ZLINK_XPUB,
  ZLINK_XSUB,
  ZLINK_XPUB_VERBOSE,
} = require('./helpers');

test('xpub-xsub: subscription across transports', async () => {
  const ctx = new zlink.Context();
  const cases = await transports('xpub');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      const xpub = new zlink.Socket(ctx, ZLINK_XPUB);
      const xsub = new zlink.Socket(ctx, ZLINK_XSUB);
      const verbose = Buffer.alloc(4);
      verbose.writeInt32LE(1, 0);
      xpub.setSockOpt(ZLINK_XPUB_VERBOSE, verbose);
      const ep = await endpointFor(tc.name, tc.endpoint, '-xpub');
      xpub.bind(ep);
      xsub.connect(ep);
      const sub = Buffer.from([1, 0x74, 0x6f, 0x70, 0x69, 0x63]);
      await sendWithRetry(xsub, sub, 0, 2000);
      const msg = await recvWithTimeout(xpub, 64, 2000);
      assert.strictEqual(msg[0], 1);
      xpub.close();
      xsub.close();
    });
  }
  ctx.close();
});
