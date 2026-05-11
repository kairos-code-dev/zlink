// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../..');

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
    const routerMonitor = router.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const dealerMonitor = dealer.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    try {
      router.bind(endpoint);
      dealer.connect(endpoint);
      routerMonitor.recv();
      dealerMonitor.recv();
    } finally {
      routerMonitor.close();
      dealerMonitor.close();
    }

    const sent = 'ping';
    dealer.send(Buffer.from(sent));

    const reply = 'pong';
    const request = new zlink.Received();
    router.recv(request);
    try {
      const recvReq = request.parts[0].data().toString();
      assert.equal(recvReq, sent);
      assert.ok(request.routingId instanceof zlink.RoutingId);
      router.send(request.routingId, Buffer.from(reply));
    } finally {
      request.close();
    }

    const response = new zlink.Received();
    dealer.recv(response);
    try {
      const recv = response.parts[0].data().toString();
      assert.equal(recv, reply);
      console.log(`[dealer-router/recv] send: "${sent}" \u2192 recv: "${recv}"`);
    } finally {
      response.close();
    }
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
