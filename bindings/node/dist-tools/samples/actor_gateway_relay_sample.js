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
function recvActorPart(actor) {
    for (let i = 0; i < 100; i += 1) {
        const part = actor.recvPart(zlink.RecvFlags.DontWait);
        if (part)
            return part;
        Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
    }
    throw new Error('actor part not received');
}
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const stream = new zlink.StreamSocket(ctx);
    let client = null;
    let actor = null;
    let session = null;
    try {
        stream.bind(endpoint);
        actor = node.actor('gateway-player-1');
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        session = await new Promise((resolve) => {
            stream.onPacket((sourceRid) => resolve(sourceRid));
            client.write(Buffer.concat([frame(Buffer.alloc(0)), frame(Buffer.from('open'))]));
        });
        stream.bindActor(node, session, actor.ref(), 2000);
        stream.sendBoundActor(node, session, 'gateway-player-1', Buffer.from('relay'));
        const part = recvActorPart(actor);
        assert.equal(part.message.data().toString(), 'relay');
        console.log('[actor/gateway] stream relayed payload to actor: "relay"');
    }
    finally {
        if (session) {
            try {
                stream.unbindActor(node, session, 'gateway-player-1', 2000);
            }
            catch (_) {
            }
        }
        if (client)
            client.destroy();
        stream.close();
        if (actor)
            actor.close(2000);
        node.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
