// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');

const SERVICE_TYPE_SPOT = 0x3002;

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function waitForTopologyEntry(registry, serviceName, endpoint) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    const entry = registry.topologySnapshot().find((item) => (
      item.serviceName === serviceName && item.endpoint === endpoint
    ));
    if (entry) {
      return entry;
    }
    await new Promise((resolve) => setImmediate(resolve));
  }
  return null;
}

async function main() {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, 'service-found');
  const node = new zlink.SpotNode(ctx);
  const pubEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const routerEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const serviceEndpoint = `tcp://127.0.0.1:${await reservePort()}`;

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    discovery.connectRegistry(routerEndpoint);
    node.attachDiscovery(discovery);
    node.bind(serviceEndpoint);

    const entry = await waitForTopologyEntry(registry, 'service-found', serviceEndpoint);
    assert.ok(entry);
    assert.equal(entry.endpoint, serviceEndpoint);
    console.log('[discovery-registry] registry: bind → discovery: connect → discover: "service-found"');
  } finally {
    discovery.close();
    registry.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
