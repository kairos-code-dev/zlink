// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
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
async function waitForTopologyEntry(query, serviceName, endpoint) {
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
        const entry = query.snapshot({ serviceName }).find((item) => item.endpoint === endpoint);
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
    const query = new zlink.RegistryQueryClient(ctx);
    const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'topology-entry-found');
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
        const entry = await waitForTopologyEntry(query, 'topology-entry-found', serviceEndpoint);
        assert.ok(entry);
        assert.equal(entry.endpoint, serviceEndpoint);
        console.log('[registry-query] connect → snapshot: "topology-entry-found"');
    }
    finally {
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
