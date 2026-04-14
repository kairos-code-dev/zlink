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

function timeoutPromise(ms, label) {
  return new Promise((_, reject) => {
    setTimeout(() => reject(new Error(`${label} timed out`)), ms);
  });
}

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const routerSocket = new zlink.RouterSocket(ctx);
  const dealerSocket = new zlink.DealerSocket(ctx);
  const clientRoutingId = zlink.RoutingId.fromBytes(Buffer.from('request-reply-client'));

  try {
    const routerMonitor = routerSocket.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    const dealerMonitor = dealerSocket.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    try {
      dealerSocket.setRoutingId(clientRoutingId);
      routerSocket.bind(endpoint);
      dealerSocket.connect(endpoint);
      routerMonitor.recv();
      dealerMonitor.recv();
    } finally {
      routerMonitor.close();
      dealerMonitor.close();
    }

    const pendingReply = dealerSocket.request(
      zlink.Message.from(Buffer.from('ping')),
      2000
    );
    const request = routerSocket.recv();
    assert.equal(request.routingId.toBytes().toString(), 'request-reply-client');
    assert.ok(typeof request.requestSeq === 'bigint');
    routerSocket.reply(request.routingId, request.requestSeq, zlink.Message.from(Buffer.from('pong')));
    const reply = await pendingReply;
    assert.equal(reply[0].data().toString(), 'pong');
    console.log('[dealer-router/request-reply/async] send: "ping" -> recv: "pong"');
  } finally {
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
