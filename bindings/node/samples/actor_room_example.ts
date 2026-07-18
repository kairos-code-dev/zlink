// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 actor 참조로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const {
  MeshPump, collectActorPayloads, destroyMeshActor, joinActorToSpot,
  leaveActorFromSpot, tcpEndpoint
} = require('./sample_support');

async function main() {
// --8<-- [start:doc]
  const ctx = zlink.createContext();
  const node = zlink.createMeshNode(ctx, { meshName: 'samples' });
  node.setBind(await tcpEndpoint());
  node.start();
  const room = node.createSpot();
  const player1 = node.createActor('player-1');
  const player2 = node.createActor('player-2');
  const pump = new MeshPump(node);
  const received: string[] = [];
  const collect = collectActorPayloads(received);

  try {
    await joinActorToSpot(pump, node, player1, room, 'enter-room', { onMessage: collect });
    await joinActorToSpot(pump, node, player2, room, 'enter-room', { onMessage: collect });

    // 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
    async function sendAndWait(actor, text: string, want: number) {
      node.sendToActor(actor, Buffer.from(text));
      await pump.pumpUntil(() => received.length >= want, 2000, collect);
    }

    await sendAndWait(player1, 'your-turn', 1);
    await sendAndWait(player2, 'wait', 2);
    assert.deepEqual(received, ['your-turn', 'wait']);

    await leaveActorFromSpot(pump, node, player1);
    await leaveActorFromSpot(pump, node, player2);
    console.log('[actor/room] player-1: "your-turn", player-2: "wait"');
  } finally {
    for (const player of [player1, player2]) {
      try {
        await destroyMeshActor(pump, node, player);
      } catch (_) {
      }
    }
    pump.close();
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
