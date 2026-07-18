// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT timer.
// 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).

'use strict';

const zlink = require('@zlink-systems/zlink');
const { tcpEndpoint } = require('./sample_support');

async function main() {
// --8<-- [start:doc]
  const ctx = zlink.createContext();
  const node = zlink.createMeshNode(ctx, { meshName: 'samples' });
  node.setBind(await tcpEndpoint());
  node.start();
  const room = node.createSpot();
  // 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
  const timer = zlink.createTimerFromSpot(room);

  try {
    let ticks = 0;
    timer.onFire(() => { ticks += 1; });
    // 50ms 간격으로 3번 발화한다.
    timer.start(50_000_000n, 3n);

    const deadline = Date.now() + 3000;
    while (ticks < 3 && Date.now() < deadline) {
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    if (ticks < 3) throw new Error(`timer fired only ${ticks} times`);
    console.log(`[spot/timer] room tick fired ${ticks} times`);
  } finally {
    timer.close();
    room.close();
    node.close();
    ctx.close();
  }
// --8<-- [end:doc]
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
