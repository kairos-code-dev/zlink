// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');
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
        const received = await new Promise((resolve, reject) => {
            try {
                stream.onReceive((message) => {
                    resolve(message);
                });
            }
            catch (error) {
                reject(error);
                return;
            }
            const sent = 'hello-stream';
            client.write(Buffer.from(sent));
        });
        assert.ok(received.routingId instanceof zlink.RoutingId);
        const recv = received.parts[0].data().toString();
        assert.equal(recv, 'hello-stream');
        stream.send(received.routingId, Buffer.from('hello-stream'));
        const [reply] = await once(client, 'data');
        assert.equal(reply.toString(), 'hello-stream');
        console.log(`[stream/callback] send: "hello-stream" \u2192 recv: "${recv}"`);
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
