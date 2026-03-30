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
        const received = await new Promise((resolve, reject) => {
            try {
                stream.recvHandler((routingId, parts) => {
                    resolve({ routingId, parts });
                });
            }
            catch (error) {
                reject(error);
                return;
            }
            client.write(Buffer.from('ping'));
        });
        assert.ok(Buffer.isBuffer(received.routingId));
        assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['ping']);
        stream.send(received.routingId, zlink.Message.copyOf('pong'));
        const [reply] = await once(client, 'data');
        assert.equal(reply.toString(), 'pong');
        console.log('stream callback sample ok');
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
