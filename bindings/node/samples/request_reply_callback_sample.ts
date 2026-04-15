// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');

async function reservePort() {
  const srv = net.createServer();
  srv.listen(0, '127.0.0.1');
  await once(srv, 'listening');
  const { port } = srv.address();
  await new Promise((resolve, reject) => srv.close((error) => error ? reject(error) : resolve()));
  return port;
}

function waitFor(label, ms) {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  const timer = setTimeout(() => reject(new Error(`${label} timed out`)), ms);
  return {
    promise: promise.finally(() => clearTimeout(timer)),
    resolve
  };
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

    const replyHandled = waitFor('reply callback', 2000);
    const requestHandled = waitFor('request callback', 2000);

    const waitForRequest = () => {
      try {
        const received = routerSocket.recv(zlink.RecvFlags.DontWait);
        try {
          assert.equal(received.routingId.toBytes().toString(), 'request-reply-client');
          assert.ok(typeof received.requestSeq === 'bigint');
          routerSocket.reply(
            received.routingId,
            received.requestSeq,
            zlink.Message.from(Buffer.from('pong'))
          );
          requestHandled.resolve();
        } finally {
          received.close();
        }
      } catch (error) {
        if (
          error instanceof zlink.RecvError
          && error.result === zlink.RecvResult.NoData
        ) {
          setImmediate(waitForRequest);
          return;
        }
        throw error;
      }
    };
    waitForRequest();

    dealerSocket.request(
      zlink.Message.from(Buffer.from('ping')),
      (result, reply) => {
        try {
          assert.equal(result, zlink.RequestResult.Ok);
          assert.equal(reply[0].data().toString(), 'pong');
          replyHandled.resolve();
        } finally {
          for (const part of reply) {
            part.close();
          }
        }
      },
      0,
      2000
    );

    await requestHandled.promise;
    await replyHandled.promise;
    console.log('[dealer-router/request-reply/callback] send: "ping" -> recv: "pong"');
  } finally {
    dealerSocket.close();
    routerSocket.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
