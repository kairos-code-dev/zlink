// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
async function reservePort() {
    const server = net.createServer();
    server.listen(0, '127.0.0.1');
    await once(server, 'listening');
    const { port } = server.address();
    await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    return port;
}
function frame(payload) {
    const framed = Buffer.allocUnsafe(payload.length + 6);
    framed.writeUInt16BE(0, 0);
    framed.writeUInt32BE(payload.length, 2);
    payload.copy(framed, 6);
    return framed;
}
function waitForJoin(spot) {
    for (let i = 0; i < 100; i += 1) {
        const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
        if (request)
            return request;
        Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
    }
    throw new Error('actor join request not received');
}
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const stream = zlink.createStreamSocket(ctx);
    let spot = null;
    let actor = null;
    let client = null;
    let session = null;
    try {
        spot = node.createSpot();
        actor = node.createActor('room-player-1');
        stream.attachActorGateway(node);
        stream.bind(endpoint);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        session = await new Promise((resolve) => {
            stream.setPacketHandler((sourceRid) => resolve(sourceRid));
            client.write(frame(Buffer.from('open')));
        });
        await stream.bindActor(session, actor.ref()).timeout(2000).submitAsync();
        const replyPromise = actor.join(spot).message(Buffer.from('join-room')).timeout(2000).submitAsync();
        const request = waitForJoin(spot);
        assert.equal(request.message.data().toString(), 'join-room');
        spot.replyActorJoin(request, 0).message(Buffer.from('welcome')).submit();
        const reply = await replyPromise;
        assert.equal(reply.result.result, zlink.RequestResult.Ok);
        assert.equal(reply.parts[0].data().toString(), 'welcome');
        await actor.leave(spot).timeout(2000).submitAsync();
        console.log('[actor/room] join accepted with reply: "welcome"');
    }
    finally {
        if (session) {
            try {
                await stream.unbindActor(session, 'room-player-1').timeout(2000).submitAsync();
            }
            catch (_) {
            }
        }
        if (client)
            client.destroy();
        stream.close();
        if (actor) {
            try {
                actor.close(2000);
            }
            catch (_) {
            }
        }
        if (spot) {
            try {
                spot.close();
            }
            catch (_) {
            }
        }
        try {
            node.close();
        }
        catch (_) {
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
