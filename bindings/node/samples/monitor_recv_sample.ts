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
  const serverMonitor = server.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
  const clientMonitor = client.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);

  try {
    server.bind(endpoint);
    client.connect(endpoint);

    const serverEvent = serverMonitor.recv();
    const clientEvent = clientMonitor.recv();
    assert.equal(serverEvent.event, zlink.MonitorEvent.CONNECTION_READY);
    assert.equal(clientEvent.event, zlink.MonitorEvent.CONNECTION_READY);
    console.log('[monitor/recv] recv: "connection-ready"');
  } finally {
    clientMonitor.close();
    serverMonitor.close();
    client.close();
    server.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
