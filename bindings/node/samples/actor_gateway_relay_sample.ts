// SPDX-License-Identifier: MPL-2.0
//
// A raw STREAM gateway bridges an external TCP client onto a mesh actor. The
// stream-session service binds the client's session to the actor, and messages
// sent over that session are relayed to the actor and drained via pull dispatch.

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
const {
  MeshPump, collectActorPayloads, destroyMeshActor, frame, joinActorToSpot,
  leaveActorFromSpot, reservePort, tcpEndpoint
} = require('./sample_support');

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = zlink.createContext();
  const node = zlink.createMeshNode(ctx, { meshName: 'samples' });
  const stream = zlink.createStreamSocket(ctx);
  let service = null;
  let pump = null;
  let spot = null;
  let actor = null;
  let client = null;

  try {
    node.setBind(await tcpEndpoint());
    node.start();
    stream.bind(endpoint);
    service = node.createStreamSessionService(stream);
    service.start();

    spot = node.createSpot();
    actor = node.createActor('play-session-actor');
    pump = new MeshPump(node);
    const payloads: string[] = [];
    const collect = collectActorPayloads(payloads);

    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');

    // The first framed packet reveals the client's STREAM session routing id.
    const session = await new Promise((resolve) => {
      stream.setPacketHandler((sourceRid) => resolve(sourceRid));
      client.write(frame(Buffer.from('open')));
    });

    // Bind the session to the actor, then join the actor into the room spot.
    const bindOperation = service.bindActor(session, actor, 2000);
    await pump.awaitCompletion(bindOperation, 2000, collect);
    await joinActorToSpot(pump, node, actor, spot, 'join-play', { onMessage: collect });

    // Relay a message to the actor over the bound session.
    service.sendToActor(session, actor, Buffer.from('client-input'));
    await pump.pumpUntil(() => payloads.length >= 1, 2000, collect);
    assert.deepEqual(payloads, ['client-input']);

    await leaveActorFromSpot(pump, node, actor);
    console.log('[actor/gateway] stream payload: "client-input" -> actor: "client-input"');
  } finally {
    try {
      if (pump && actor) await destroyMeshActor(pump, node, actor);
    } catch (_) {
    }
    if (client) client.destroy();
    if (pump) pump.close();
    if (service) service.close();
    stream.close();
    if (spot) spot.close();
    node.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
