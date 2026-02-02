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
  ZLINK_PUB,
  ZLINK_SUB,
  ZLINK_SUBSCRIBE,
} = require('./helpers');

test('pubsub: messaging across transports', async () => {
  const ctx = new zlink.Context();
  const cases = await transports('pubsub');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      const pub = new zlink.Socket(ctx, ZLINK_PUB);
      const sub = new zlink.Socket(ctx, ZLINK_SUB);
      const ep = await endpointFor(tc.name, tc.endpoint, '-pubsub');
      pub.bind(ep);
      sub.connect(ep);
      sub.setSockOpt(ZLINK_SUBSCRIBE, Buffer.from('topic'));
      await new Promise(r => setTimeout(r, 50));
      await sendWithRetry(pub, Buffer.from('topic payload'), 0, 2000);
      const buf = await recvWithTimeout(sub, 64, 2000);
      assert.ok(buf.toString().startsWith('topic'));
      pub.close();
      sub.close();
    });
  }
  ctx.close();
});
