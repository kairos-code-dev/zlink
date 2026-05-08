// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../..');

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
    actor = node.createActor('room-player-1');
    stream.bind(endpoint);
    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');
    session = await new Promise((resolve) => {
      stream.onPacket((sourceRid) => resolve(sourceRid));
      client.write(frame(Buffer.from('open')));
    });
    stream.bindActor(node, session, actor.ref(), 2000);

    const replyPromise = new Promise((resolve) => {
      actor.join(spot, Buffer.from('join-room'), (result, parts) => {
        resolve({ result, parts });
      }, zlink.SendFlags.None, 2000);
    });

    const request = waitForJoin(spot);
    assert.equal(request.message.data().toString(), 'join-room');
    spot.replyActorJoin(request, true, Buffer.from('welcome'));

    const reply = await replyPromise;
    assert.equal(reply.result, zlink.RequestResult.Ok);
    assert.equal(reply.parts[0].data().toString(), 'welcome');
    actor.leave(spot);
    console.log('[actor/room] join accepted with reply: "welcome"');
  } finally {
    if (session) {
      try {
        stream.unbindActor(node, session, 'room-player-1', 2000);
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
