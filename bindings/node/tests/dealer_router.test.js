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
  ZLINK_DEALER,
  ZLINK_ROUTER,
  ZLINK_SNDMORE,
} = require('./helpers');

test('dealer-router: request/reply across transports', async () => {
  const ctx = new zlink.Context();
  const cases = await transports('dealer-router');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      const router = new zlink.Socket(ctx, ZLINK_ROUTER);
      const dealer = new zlink.Socket(ctx, ZLINK_DEALER);
      const ep = await endpointFor(tc.name, tc.endpoint, '-dr');
      router.bind(ep);
      dealer.connect(ep);
      await new Promise(r => setTimeout(r, 50));
      await sendWithRetry(dealer, Buffer.from('hello'), 0, 2000);
      const rid = await recvWithTimeout(router, 256, 2000);
      const payload = await recvWithTimeout(router, 256, 2000);
      assert.strictEqual(payload.toString().trim(), 'hello');
      router.send(rid, ZLINK_SNDMORE);
      await sendWithRetry(router, Buffer.from('world'), 0, 2000);
      const resp = await recvWithTimeout(dealer, 64, 2000);
      assert.strictEqual(resp.toString().trim(), 'world');
      router.close();
      dealer.close();
    });
  }
  ctx.close();
});
