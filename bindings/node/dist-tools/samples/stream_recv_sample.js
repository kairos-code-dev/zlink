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
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = new zlink.Context();
    const stream = new zlink.StreamSocket(ctx);
    let client;
    try {
        stream.bind(endpoint);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        const sent = 'hello-stream';
        client.write(Buffer.from(sent));
        const received = stream.recv();
        assert.ok(Buffer.isBuffer(received.routingId));
        const recv = received.parts[0].data.toString();
        assert.equal(recv, sent);
        stream.send(received.routingId, Buffer.from(sent));
        const [reply] = await once(client, 'data');
        assert.equal(reply.toString(), sent);
        console.log(`[stream/recv] send: "${sent}" \u2192 recv: "${recv}"`);
    }
    finally {
        if (client) {
            client.destroy();
        }
        stream.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
