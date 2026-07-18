// SPDX-License-Identifier: MPL-2.0
//
// End-to-end room server: a TCP client reaches a mesh actor through a STREAM
// gateway. The join round-trip and the relayed payload are asserted explicitly.

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('@zlink-systems/zlink');
const {
  MeshPump, collectActorPayloads, destroyMeshActor, frame, isActorJoinRequest,
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
    actor = node.createActor('room-player-1');
    pump = new MeshPump(node);
    const payloads: string[] = [];
    const collect = collectActorPayloads(payloads);

    client = net.createConnection({ host: '127.0.0.1', port });
    await once(client, 'connect');
    const session = await new Promise((resolve) => {
      stream.setPacketHandler((sourceRid) => resolve(sourceRid));
      client.write(frame(Buffer.from('open')));
    });
    await pump.awaitCompletion(service.bindActor(session, actor, 2000), 2000, collect);

    // Join the actor into the room and accept it, capturing the join request.
    const nodeRid = node.status().routingId;
    const joinOperation = node.joinActorSpot(
      actor, nodeRid, spot.routingId, spot.status().lifecycleGeneration,
      Buffer.from('enter-room'), 2000);
    let joinRequestPayload = null;
    const joinCompletion = await pump.awaitCompletion(joinOperation, 2000, (record) => {
      if (isActorJoinRequest(record)) {
        joinRequestPayload = record.parts[0].data().toString();
        record.replyActorJoin(0, Buffer.from('accepted'));
      } else {
        collect(record);
      }
    });
    assert.equal(joinRequestPayload, 'enter-room');
    assert.equal(joinCompletion.terminalResult, zlink.RequestResult.Ok);
    assert.equal(joinCompletion.parts[0].data().toString(), 'accepted');

    service.sendToActor(session, actor, Buffer.from('move:north'));
    await pump.pumpUntil(() => payloads.length >= 1, 2000, collect);
    assert.deepEqual(payloads, ['move:north']);

    await leaveActorFromSpot(pump, node, actor);
    console.log('[actor/room] stream payload: "move:north" -> actor: "move:north"');
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
