// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// 한 파일 안에 전체 흐름이 들어 있다 — Node 바인딩만 있으면 그대로 실행된다:
//   ./run_samples.sh 가 만드는 런타임 링크를 쓰거나
//   node actor_queue_example.js  (@zlink-systems/zlink 해석 가능 환경)

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');

// join 요청이 도착할 때까지 잠깐 기다렸다 받아온다.
function waitForJoin(spot) {
  for (let i = 0; i < 200; i += 1) {
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) return request;
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
  }
  throw new Error('actor join request not received');
}

async function main() {
  const ctx = zlink.createContext();
  const node = zlink.createSpotNode(ctx);
  const spot = node.createSpot();
  const actor = node.createActor('single-player');
  const stream = zlink.createStreamSocket(ctx);
  const payloads = [];

  try {
    // 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
    // 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
    stream.attachActorGateway(node);
    const session = zlink.RoutingId.from(Buffer.from('single-player-session'));
    await stream.bindActor(session, actor.ref()).timeout(2000).submitAsync();

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
    async function joinAndAccept(payload) {
      const reply = actor.join(spot).message(Buffer.from(payload)).timeout(2000).submitAsync();
      const request = waitForJoin(spot);
      spot.replyActorJoin(request, 0).message(Buffer.from('accepted')).submit();
      await reply;
    }

    await joinAndAccept('join-first'); // actor가 spot에 합류
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('before')).submit(); // joined 상태에서 도착
    await actor.leave(spot).timeout(2000).submitAsync(); // 처리 위치 이탈 (세션 바인딩은 유지)
    stream.sendBoundActor(session, 'single-player').message(Buffer.from('between')).submit(); // leave 사이 도착 → 큐잉
    await joinAndAccept('join-second'); // rejoin → 큐된 메시지가 핸들러로 배달된다

    for (let i = 0; i < 200 && payloads.length < 2; i += 1) {
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    assert.deepEqual(payloads, ['before', 'between']);
    await actor.leave(spot).timeout(2000).submitAsync();
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
