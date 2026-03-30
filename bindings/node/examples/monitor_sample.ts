// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const socket = new zlink.PairSocket(ctx);
  const monitor = socket.monitorOpen(zlink.MonitorEvent.ALL);
  let client;

  try {
    assert.equal(monitor.tryRecv(), null);
    socket.bind(endpoint);
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');

    const event = monitor.recv();
    assert.equal(event.event, zlink.MonitorEvent.LISTENING);
    console.log('monitor sample ok');
  } finally {
    if (client) {
      client.destroy();
    }
    monitor.close();
    socket.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
