// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');

async function reservePort() {
  const srv = net.createServer();
  srv.listen(0, '127.0.0.1');
  await once(srv, 'listening');
  const { port } = srv.address();
  await new Promise((resolve, reject) => srv.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);

  try {
    const routerMonitor = router.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    const dealerMonitor = dealer.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    try {
      router.bind(endpoint);
      dealer.connect(endpoint);
      routerMonitor.recv();
      dealerMonitor.recv();
    } finally {
      routerMonitor.close();
      dealerMonitor.close();
    }

    const replyPromise = new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('dealer-router callback sample timed out'));
      }, 5000);

      router.onReceive((routingId, parts) => {
        const payload = parts[0].data.toString();
        assert.ok(Buffer.isBuffer(routingId));
        assert.equal(payload, 'ping');
        router.send(routingId, Buffer.from('pong'));
      });

      dealer.onReceive((routingId, parts) => {
        clearTimeout(timeout);
        resolve({ routingId, parts });
      });
    });

    const sent = 'ping';
    dealer.send(Buffer.from(sent));

    const response = await replyPromise;
    const recv = response.parts[0].data.toString();
    assert.equal(recv, 'pong');
    console.log(`[dealer-router/callback] send: "${sent}" \u2192 recv: "${recv}"`);
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
