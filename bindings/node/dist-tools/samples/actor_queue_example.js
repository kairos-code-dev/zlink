// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: Actor 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다. actor가 spot을 떠나 있는 동안 도착한
// 메시지는 큐잉되고, 다시 join하면 순서대로 배달된다.
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { MeshPump, collectActorPayloads, destroyMeshActor, joinActorToSpot, leaveActorFromSpot, tcpEndpoint } = require('./sample_support');
async function main() {
    const ctx = zlink.createContext();
    const node = zlink.createMeshNode(ctx, { meshName: 'samples' });
    node.setBind(await tcpEndpoint());
    node.start();
    const spot = node.createSpot();
    const actor = node.createActor('single-player');
    const pump = new MeshPump(node);
    const payloads = [];
    // 어느 pump 단계에서 도착하든 놓치지 않도록, 모든 드레인에 같은 수집기를 넘긴다.
    const collect = collectActorPayloads(payloads);
    try {
        await joinActorToSpot(pump, node, actor, spot, 'join-first', { onMessage: collect });
        node.sendToActor(actor, Buffer.from('before')); // joined 상태에서 도착
        await leaveActorFromSpot(pump, node, actor, { onMessage: collect }); // 처리 위치 이탈
        node.sendToActor(actor, Buffer.from('between')); // leave 사이 도착 → 큐잉
        await joinActorToSpot(pump, node, actor, spot, 'join-second', { onMessage: collect }); // rejoin
        await pump.pumpUntil(() => payloads.length >= 2, 2000, collect);
        assert.deepEqual(payloads, ['before', 'between']);
        await leaveActorFromSpot(pump, node, actor);
        console.log('[actor/single-player] queued payload: "before/between" -> actor: "before/between"');
    }
    finally {
        try {
            await destroyMeshActor(pump, node, actor);
        }
        catch (_) {
        }
        pump.close();
        spot.close();
        node.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
