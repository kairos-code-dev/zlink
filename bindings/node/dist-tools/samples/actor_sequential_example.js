// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
// 도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
function waitForJoin(spot) {
    for (let i = 0; i < 200; i += 1) {
        const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
        if (request)
            return request;
        Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
    }
    throw new Error('actor join request not received');
}
async function main() {
    // --8<-- [start:doc]
    const ctx = zlink.createContext();
    const node = zlink.createSpotNode(ctx);
    const room = node.createSpot();
    // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
    const player = node.createActor('player');
    const stream = zlink.createStreamSocket(ctx);
    const processed = [];
    try {
        stream.attachActorGateway(node);
        const session = zlink.RoutingId.from(Buffer.from('player-session'));
        // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
        await stream.bindActor(session, player.ref()).timeout(2000).submitAsync();
        // dispatch 핸들러: STREAM이 relay한 메시지를 모은다.
        room.setDispatchHandler((info) => {
            if (info.event !== zlink.SpotDispatchEvent.ActorReadable)
                return;
            for (;;) {
                const part = info.recvActorPart(zlink.RecvFlags.DontWait);
                if (!part)
                    return;
                processed.push(part.message.data().toString());
            }
        });
        // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
        const reply = player.join(room).message(Buffer.from('enter-room')).timeout(2000).submitAsync();
        const request = waitForJoin(room);
        room.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
        await reply;
        // STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
        const commands = ['move', 'attack', 'loot'];
        for (const command of commands) {
            stream.sendBoundActor(session, 'player').message(Buffer.from(command)).submit();
        }
        for (let i = 0; i < 200 && processed.length < commands.length; i += 1) {
            await new Promise((resolve) => setTimeout(resolve, 10));
        }
        assert.deepEqual(processed, ['move', 'attack', 'loot']);
        await player.leave(room).timeout(2000).submitAsync();
        console.log('[actor/sequential] processed in order: move -> attack -> loot');
    }
    finally {
        stream.close();
        player.close(2000);
        room.close();
        node.close();
        ctx.close();
    }
    // --8<-- [end:doc]
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
