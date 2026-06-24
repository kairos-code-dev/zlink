// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');

function waitForJoin(spot) {
  for (let i = 0; i < 200; i += 1) {
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) return request;
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
  }
  throw new Error('actor join request not received');
}

async function main() {
// --8<-- [start:doc]
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const room = node.createSpot();
  const player1 = node.createActor('player-1');
  const player2 = node.createActor('player-2');
  const stream = zlink.createStreamSocket(ctx);
  const received = [];

  try {
    const session = zlink.RoutingId.from(Buffer.from('game-room-session'));
    await stream.bindActor(session, player1.ref()).timeout(2000).submit();
    await stream.bindActor(session, player2.ref()).timeout(2000).submit();

    // dispatch 핸들러: 도착한 메시지를 모은다.
    room.setDispatchHandler((info) => {
      if (info.event !== zlink.SpotDispatchEvent.ActorReadable) return;
      for (;;) {
        const part = info.recvActorPart(zlink.RecvFlags.DontWait);
        if (!part) return;
        received.push(part.message.data().toString());
      }
    });

    async function joinAndAccept(actor) {
      const reply = actor.join(room).message(Buffer.from('enter-room')).timeout(2000).submit();
      const request = waitForJoin(room);
      room.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
      await reply;
    }

    // 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
    async function sendAndWait(actorId, text, want) {
      stream.sendBoundActor(session, actorId).message(Buffer.from(text)).submit();
      for (let i = 0; i < 200 && received.length < want; i += 1) {
        await new Promise((resolve) => setTimeout(resolve, 10));
      }
      assert.ok(received.length >= want, `message to ${actorId} not delivered`);
    }

    await joinAndAccept(player1);
    await joinAndAccept(player2);

    // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
    await sendAndWait('player-1', 'your-turn', 1);
    await sendAndWait('player-2', 'wait', 2);
    assert.deepEqual(received, ['your-turn', 'wait']);

    await player1.leave(room).timeout(2000).submit();
    await player2.leave(room).timeout(2000).submit();
    console.log('[actor/room] player-1: "your-turn", player-2: "wait"');
  } finally {
    stream.close();
    player1.close(2000);
    player2.close(2000);
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
