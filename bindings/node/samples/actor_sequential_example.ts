// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: Actor가 메시지를 들어온 순서대로 처리한다.
// Actor는 생성 직후 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. room에 합류한 뒤 actor로 보낸 메시지는 pull dispatch를 통해 들어온
// 순서 그대로 도착한다. (라우팅 평면 없이 한 노드 안에서 보여 준다.)

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const {
  MeshPump, MeshRecordKind, destroyMeshActor, joinActorToSpot,
  leaveActorFromSpot, tcpEndpoint
} = require('./sample_support');

async function main() {
// --8<-- [start:doc]
  const ctx = zlink.createContext();
  const node = zlink.createMeshNode(ctx, { meshName: 'samples' });
  node.setBind(await tcpEndpoint());
  node.start();
  const room = node.createSpot();
  // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
  const player = node.createActor('player');
  const pump = new MeshPump(node);
  const processed: string[] = [];

  try {
    // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
    await joinActorToSpot(pump, node, player, room, 'enter-room');

    // 플레이어 입력을 연달아 보낸다 — actor는 순서대로 처리한다.
    const commands = ['move', 'attack', 'loot'];
    for (const command of commands) {
      node.sendToActor(player, Buffer.from(command));
    }
    await pump.pumpUntil(
      () => processed.length >= commands.length,
      2000,
      (record) => {
        if (record.kind === MeshRecordKind.ActorSend) {
          for (const part of record.parts) processed.push(part.data().toString());
        }
      });
    assert.deepEqual(processed, ['move', 'attack', 'loot']);

    await leaveActorFromSpot(pump, node, player);
    console.log('[actor/sequential] processed in order: move -> attack -> loot');
  } finally {
    try {
      await destroyMeshActor(pump, node, player);
    } catch (_) {
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
