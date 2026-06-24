// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 빌드·실행된다.

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { waitActorJoin } = require('./sample_support');

async function main() {
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();
  const actor = node.createActor('single-player');
  const stream = zlink.createStreamSocket(ctx);
  const payloads: string[] = [];

  try {
    // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
    // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
    const session = zlink.RoutingId.from(Buffer.from('single-player-session'));
    await stream.bindActor(session, actor.ref()).timeout(2000).submit();

    // dispatch 핸들러: actor에게 온 메시지를 모은다.
    spot.setDispatchHandler((info) => {
      if (info.event !== zlink.SpotDispatchEvent.ActorReadable) return;
      for (;;) {
        const part = info.recvActorPart(zlink.RecvFlags.DontWait);
        if (!part) return;
        payloads.push(part.message.data().toString());
      }
    });

    // join 요청을 수신해 "accepted"로 응답한다.
    async function joinAndAccept(payload: string) {
      const reply = actor.join(spot).message(Buffer.from(payload)).timeout(2000).submit();
      const request = waitActorJoin(spot);
      spot.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
      await reply;
    }

    await joinAndAccept('join-first'); // actor가 spot에 합류
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('before')).submit(); // joined 상태에서 도착
    await actor.leave(spot).timeout(2000).submit(); // 처리 위치 이탈 (세션 바인딩은 유지)
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('between')).submit(); // leave 사이 도착 → 큐잉
    await joinAndAccept('join-second'); // rejoin → 큐된 메시지가 핸들러로 배달된다

    for (let i = 0; i < 200 && payloads.length < 2; i += 1) {
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    assert.deepEqual(payloads, ['before', 'between']);
    await actor.leave(spot).timeout(2000).submit();
    console.log('[actor/single-player] queued payload: "before/between" -> actor: "before/between"');
  } finally {
    stream.close();
    actor.close(2000);
    spot.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
