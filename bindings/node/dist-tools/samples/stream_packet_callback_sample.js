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
function frame(payload) {
    const framed = Buffer.allocUnsafe(payload.length + 4);
    framed.writeUInt32BE(payload.length, 0);
    payload.copy(framed, 4);
    return framed;
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
                stream.onPacket((sourceRid, header, body) => {
                    resolve({ sourceRid, header, body });
                });
            }
            catch (error) {
                reject(error);
                return;
            }
            const payload = Buffer.from('hello-stream');
            client.write(Buffer.concat([frame(Buffer.alloc(0)), frame(payload)]));
        });
        try {
            assert.ok(received.sourceRid instanceof zlink.RoutingId);
            assert.equal(received.header.data().length, 0);
            assert.equal(received.body.data().toString(), 'hello-stream');
            console.log('[stream/packet-callback] send: "hello-stream" -> recv: "hello-stream"');
        }
        finally {
            received.header.close();
            received.body.close();
        }
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
