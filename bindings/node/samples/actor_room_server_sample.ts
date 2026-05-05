// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const zlink = require('../dist/canonical');

function waitForJoin(spot) {
  for (let i = 0; i < 100; i += 1) {
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) return request;
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
  }
  throw new Error('actor join request not received');
}

async function main() {
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  let spot = null;
  let actor = null;

  try {
    spot = node.createSpot();
    actor = node.actor('room-player-1');
    const replyPromise = new Promise((resolve) => {
      actor.join(spot, Buffer.from('join-room'), (result, parts) => {
        resolve({ result, parts });
      }, zlink.SendFlags.None, 2000);
    });

    const request = waitForJoin(spot);
    assert.equal(request.message.data().toString(), 'join-room');
    spot.replyActorJoin(request.info, true, Buffer.from('welcome'));

    const reply = await replyPromise;
    assert.equal(reply.result, zlink.RequestResult.Ok);
    assert.equal(reply.parts[0].data().toString(), 'welcome');
    actor.leave(spot);
    console.log('[actor/room] join accepted with reply: "welcome"');
  } finally {
    if (actor) actor.close(2000);
    if (spot) spot.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
