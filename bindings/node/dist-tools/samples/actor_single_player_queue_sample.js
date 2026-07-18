// SPDX-License-Identifier: MPL-2.0
//
// Reconnect portability over a STREAM gateway: a session stays bound to its
// actor across a leave, so a message that arrives while the actor is away is
// queued and delivered in order once it rejoins.
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
const { MeshPump, collectActorPayloads, destroyMeshActor, frame, joinActorToSpot, leaveActorFromSpot, reservePort, tcpEndpoint } = require('./sample_support');
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = zlink.createContext();
    const node = zlink.createMeshNode(ctx, { meshName: 'samples' });
    const stream = zlink.createStreamSocket(ctx);
    let service = null;
    let pump = null;
    let spot = null;
    let actor = null;
    let client = null;
    try {
        node.setBind(await tcpEndpoint());
        node.start();
        stream.bind(endpoint);
        service = node.createStreamSessionService(stream);
        service.start();
        spot = node.createSpot();
        actor = node.createActor('single-player');
        pump = new MeshPump(node);
        const payloads = [];
        const collect = collectActorPayloads(payloads);
        client = net.createConnection({ host: '127.0.0.1', port });
        await once(client, 'connect');
        const session = await new Promise((resolve) => {
            stream.setPacketHandler((sourceRid) => resolve(sourceRid));
            client.write(frame(Buffer.from('open')));
        });
        await pump.awaitCompletion(service.bindActor(session, actor, 2000), 2000, collect);
        await joinActorToSpot(pump, node, actor, spot, 'join-first', { onMessage: collect });
        service.sendToActor(session, actor, Buffer.from('before')); // joined 상태에서 도착
        await leaveActorFromSpot(pump, node, actor, { onMessage: collect }); // 처리 위치 이탈
        service.sendToActor(session, actor, Buffer.from('between')); // leave 사이 도착 → 큐잉
        await joinActorToSpot(pump, node, actor, spot, 'join-second', { onMessage: collect }); // rejoin
        await pump.pumpUntil(() => payloads.length >= 2, 2000, collect);
        assert.deepEqual(payloads, ['before', 'between']);
        await leaveActorFromSpot(pump, node, actor);
        console.log('[actor/single-player] queued payload: "before/between" -> actor: "before/between"');
    }
    finally {
        try {
            if (pump && actor)
                await destroyMeshActor(pump, node, actor);
        }
        catch (_) {
        }
        if (client)
            client.destroy();
        if (pump)
            pump.close();
        if (service)
            service.close();
        stream.close();
        if (spot)
            spot.close();
        node.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
