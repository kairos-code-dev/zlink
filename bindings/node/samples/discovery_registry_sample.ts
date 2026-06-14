// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { tcpEndpoint } = require('./sample_support');

const AUTO_CONNECT_SPOT_MESH = 5;

async function waitForTopologyEntry(registry, channelName, endpoint) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    const entry = registry.topology().find((item) => item.channelName === channelName);
    if (entry) {
      return entry;
    }
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  return null;
}

async function main() {
// --8<-- [start:doc]
  const ctx = zlink.createContext();
  const registry = zlink.createRegistry(ctx);
  const discovery = zlink.createDiscovery(ctx, AUTO_CONNECT_SPOT_MESH, 'sample');
  const node = zlink.createSpotNode(ctx);
  const pubEndpoint = await tcpEndpoint();
  const routerEndpoint = await tcpEndpoint();
  const serviceEndpoint = await tcpEndpoint();

  try {
    registry.bind(pubEndpoint, routerEndpoint);
    discovery.connectRegistry(routerEndpoint);
    node.attachDiscovery(discovery);
    node.setPubBind(serviceEndpoint);

  const entry = await waitForTopologyEntry(registry, 'sample', serviceEndpoint);
  assert.ok(entry);
  console.log('[discovery-registry] service: "sample" -> discovered');
  } finally {
    node.close();
    discovery.close();
    registry.close();
    ctx.close();
  }
// --8<-- [end:doc]
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
