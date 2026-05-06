// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

function frame(payload) {
  const framed = Buffer.allocUnsafe(payload.length + 4);
  framed.writeUInt32BE(payload.length, 0);
  payload.copy(framed, 4);
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

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const stream = new zlink.StreamSocket(ctx);
  let spot = null;
  let client = null;
  let actor = null;
  let session = null;

  try {
    spot = node.createSpot();
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
    actor = node.actor('gateway-player-1');
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');

    session = await new Promise((resolve) => {
      stream.onPacket((sourceRid) => resolve(sourceRid));
      client.write(Buffer.concat([frame(Buffer.alloc(0)), frame(Buffer.from('open'))]));
    });

    stream.bindActor(node, session, actor.ref(), 2000);
    const joinReply = new Promise((resolve) => {
      actor.join(spot, Buffer.from('join-gateway'), (result, parts) => {
        resolve({ result, parts });
      }, zlink.SendFlags.None, 2000);
    });
    const joinRequest = waitForJoin(spot);
    spot.replyActorJoin(joinRequest.info, true, Buffer.from('ok'));
    await joinReply;
    stream.sendBoundActor(node, session, 'gateway-player-1', Buffer.from('relay'));

    for (let i = 0; i < 100 && payloads.length === 0; i += 1) {
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
    assert.deepEqual(payloads, ['relay']);
    console.log('[actor/gateway] stream relayed payload to actor: "relay"');
  } finally {
    if (session) {
      try {
        stream.unbindActor(node, session, 'gateway-player-1', 2000);
      } catch (_) {
      }
    }
    if (client) client.destroy();
    stream.close();
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
