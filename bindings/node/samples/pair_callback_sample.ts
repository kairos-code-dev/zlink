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

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const server = new zlink.PairSocket(ctx);
  const client = new zlink.PairSocket(ctx);

  try {
    const serverMonitor = server.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    const clientMonitor = client.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    try {
      server.bind(endpoint);
      client.connect(endpoint);
      serverMonitor.recv();
      clientMonitor.recv();
    } finally {
      serverMonitor.close();
      clientMonitor.close();
    }

    const received = await new Promise((resolve, reject) => {
      try {
        const deadline = Date.now() + 5000;
        const poll = () => {
          try {
            resolve(server.recv(zlink.RecvFlags.DontWait));
            return;
          } catch (error) {
            if (
              error instanceof zlink.RecvError
              && error.result === zlink.RecvResult.NoData
            ) {
              if (Date.now() >= deadline) {
                reject(new Error('pair callback sample timed out'));
                return;
              }
              setImmediate(poll);
              return;
            }
            reject(error);
          }
        };
        poll();
      } catch (error) {
        reject(error);
        return;
      }
      const sent = 'hello-pair';
      client.send(Buffer.from(sent));
    });

    assert.ok(received.routingId instanceof zlink.RoutingId);
    const recv = received.parts[0].data().toString();
    assert.equal(recv, 'hello-pair');
    console.log(`[pair/callback] send: "hello-pair" \u2192 recv: "${recv}"`);
  } finally {
    client.close();
    server.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
