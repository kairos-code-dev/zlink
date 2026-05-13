// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

function frame(payload) {
  const framed = Buffer.allocUnsafe(payload.length + 6);
  framed.writeUInt16BE(0, 0);
  framed.writeUInt32BE(payload.length, 2);
  payload.copy(framed, 6);
  return framed;
}

function waitForJoin(spot) {
  for (let i = 0; i < 100; i += 1) {
    const request = spot.recvActorJoin(zlink.RecvFlags.DontWait);
    if (request) return request;
    Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 10);
  }
  throw new Error('actor join request not received');
}

async function acceptJoin(actor, spot, payload) {
  const replyPromise = actor.join(spot).message(Buffer.from(payload)).timeout(2000).submitAsync();
  const request = waitForJoin(spot);
  spot.replyActorJoin(request, true).message(Buffer.from('ok')).submit();
  const reply = await replyPromise;
  assert.equal(reply.result.result, zlink.RequestResult.Ok);
}

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const stream = new zlink.StreamSocket(ctx);
  let spot = null;
  let actor = null;
  let client = null;
  let session = null;

  try {
    spot = node.createSpot();
    actor = node.createActor('queue-player-1');
    const payloads = [];
    spot.onDispatchEvent((info) => {
      if (info.event !== zlink.SpotDispatchEvent.ActorReadable) {
        return;
      }
      for (;;) {
        const part = info.recvActorPart(zlink.RecvFlags.DontWait);
        if (!part) return;
        payloads.push(part.message.data().toString());
      }
    });
    stream.bind(endpoint);
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');
    session = await new Promise((resolve) => {
      stream.onPacket((sourceRid) => resolve(sourceRid));
      client.write(frame(Buffer.from('open')));
    });
    await stream.bindActor(session, actor.ref()).timeout(2000).submitAsync();
    await acceptJoin(actor, spot, 'first-join');

    await actor.leave(spot).timeout(2000).submitAsync();
    stream.sendBoundActor(session, 'queue-player-1').message(Buffer.from('queued')).submit();
    await acceptJoin(actor, spot, 'second-join');

    for (let i = 0; i < 100 && payloads.length === 0; i += 1) {
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    assert.deepEqual(payloads, ['queued']);
    await actor.leave(spot).timeout(2000).submitAsync();
    console.log('[actor/queue] queued payload survived leave and rejoin');
  } finally {
    if (session) {
      try {
        await stream.unbindActor(session, 'queue-player-1').timeout(2000).submitAsync();
      } catch (_) {
      }
    }
    if (client) client.destroy();
    stream.close();
    if (actor) {
      try {
        actor.close(2000);
      } catch (_) {
      }
    }
    if (spot) {
      try {
        spot.close();
      } catch (_) {
      }
    }
    try {
      node.close();
    } catch (_) {
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
