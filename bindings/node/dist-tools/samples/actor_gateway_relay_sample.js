// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
const { frame, reservePort, waitActorJoin } = require('./sample_support');
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const stream = zlink.createStreamSocket(ctx);
    let spot = null;
    let client = null;
    let actor = null;
    let session = null;
    try {
        spot = node.createSpot();
        const payloads = [];
        spot.setDispatchHandler((info) => {
            if (info.event !== zlink.SpotDispatchEvent.ActorReadable) {
                return;
            }
            for (;;) {
                const part = info.recvActorPart(zlink.RecvFlags.DontWait);
                if (!part)
                    return;
                payloads.push(part.message.data().toString());
            }
        });
        stream.attachActorGateway(node);
        stream.bind(endpoint);
        actor = node.createActor('play-session-actor');
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        session = await new Promise((resolve) => {
            stream.setPacketHandler((sourceRid) => resolve(sourceRid));
            client.write(frame(Buffer.from('open')));
        });
        await stream.bindActor(session, actor.ref()).timeout(2000).submit();
        const joinReply = actor.join(spot).message(Buffer.from('join-play')).timeout(2000).submit();
        const joinRequest = waitActorJoin(spot);
        spot.replyActorJoin(joinRequest, 0).message(Buffer.from('accepted')).submit();
        await joinReply;
        stream.sendBoundActor(session, 'play-session-actor').message(Buffer.from('client-input')).submit();
        for (let i = 0; i < 100 && payloads.length === 0; i += 1) {
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
        assert.deepEqual(payloads, ['client-input']);
        await actor.leave(spot).timeout(2000).submit();
        console.log('[actor/gateway] stream payload: "client-input" -> actor: "client-input"');
    }
    finally {
        if (session) {
            try {
                await stream.unbindActor(session, 'play-session-actor').timeout(2000).submit();
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
