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
  ZLINK_SNDMORE,
} = require('./helpers');

test('multipart: pair across transports', async () => {
  const ctx = new zlink.Context();
  const cases = await transports('multipart');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      const a = new zlink.Socket(ctx, ZLINK_PAIR);
      const b = new zlink.Socket(ctx, ZLINK_PAIR);
      const ep = await endpointFor(tc.name, tc.endpoint, '-mp');
      a.bind(ep);
      b.connect(ep);
      await new Promise(r => setTimeout(r, 50));
      await sendWithRetry(b, Buffer.from('a'), ZLINK_SNDMORE, 2000);
      await sendWithRetry(b, Buffer.from('b'), 0, 2000);
      const p1 = await recvWithTimeout(a, 16, 2000);
      const p2 = await recvWithTimeout(a, 16, 2000);
      assert.strictEqual(p1.toString().trim(), 'a');
      assert.strictEqual(p2.toString().trim(), 'b');
      a.close();
      b.close();
    });
  }
  ctx.close();
});
