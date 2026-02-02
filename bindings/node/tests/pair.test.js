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
  ZLINK_PAIR,
} = require('./helpers');

test('pair: messaging across transports', async () => {
  const ctx = new zlink.Context();
  const cases = await transports('pair');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      const a = new zlink.Socket(ctx, ZLINK_PAIR);
      const b = new zlink.Socket(ctx, ZLINK_PAIR);
      const ep = await endpointFor(tc.name, tc.endpoint, '-pair');
      a.bind(ep);
      b.connect(ep);
      await new Promise(r => setTimeout(r, 50));
      await sendWithRetry(b, Buffer.from('ping'), 0, 2000);
      const out = await recvWithTimeout(a, 16, 2000);
      assert.strictEqual(out.toString('utf8', 0, 4), 'ping');
      a.close();
      b.close();
    });
  }
  ctx.close();
});
