// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../..');

const AUTO_CONNECT_SPOT_MESH = 5;

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function waitForTopologyEntry(query, channelName, endpoint) {
  const deadline = Date.now() + 15000;
  while (Date.now() < deadline) {
    const entry = query.snapshot({ channelName }).find((item) => item.channelName === channelName);
    if (entry) {
      return entry;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  return null;
}

async function main() {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const query = new zlink.RegistryQueryClient(ctx);
  const discovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, 'sample');
  const node = new zlink.SpotNode(ctx);
  const pubEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const routerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const serviceEndpoint = `tcp://127.0.0.1:${await reservePort()}`;

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    query.connect(routerEndpoint);
    discovery.connectRegistry(routerEndpoint);
    node.attachDiscovery(discovery);
    node.bind(serviceEndpoint);

    const entry = await waitForTopologyEntry(query, 'sample', serviceEndpoint);
    assert.ok(entry);
    console.log('[registry-query] service: "sample" -> snapshot: found');
  } finally {
    node.close();
    discovery.close();
    query.close();
    registry.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
