// SPDX-License-Identifier: MPL-2.0
//
// A spot issues a request across a mesh channel and a peer node replies. The
// route bridge is gone in RouteMesh 10.0.0: both nodes simply advertise the
// channel, and the request/reply completion arrives through pull dispatch.

'use strict';

const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const {
  MeshPump, MeshRecordKind, isRequestRecord, operationIdEquals,
  tcpEndpoint, waitPeerAdmitted, waitUntil
} = require('./sample_support');

const REQUEST_PAYLOAD = 'spot-ping';
const REPLY_PAYLOAD = 'spot-pong';
const CHANNEL_NAME = 'orders';

async function main() {
  const requesterCtx = zlink.createContext();
  const responderCtx = zlink.createContext();
  const requesterNode = zlink.createMeshNode(requesterCtx, { meshName: 'samples' });
  const responderNode = zlink.createMeshNode(responderCtx, { meshName: 'samples' });
  let requesterPump = null;
  let responderPump = null;
  let requester = null;

  try {
    const requesterEndpoint = await tcpEndpoint();
    const responderEndpoint = await tcpEndpoint();
    requesterNode.setBind(requesterEndpoint);
    responderNode.setBind(responderEndpoint);
    // Both nodes must be members of the channel to route requests over it.
    requesterNode.addChannelName(CHANNEL_NAME);
    responderNode.addChannelName(CHANNEL_NAME);
    requesterNode.start();
    responderNode.start();
    requesterNode.connectPeer({ endpoint: responderEndpoint });
    responderNode.connectPeer({ endpoint: requesterEndpoint });

    requester = requesterNode.createSpot();
    requesterPump = new MeshPump(requesterNode);
    responderPump = new MeshPump(responderNode);

    await waitPeerAdmitted(requesterNode);
    await waitPeerAdmitted(responderNode);

    const operationId = requester.requestToChannel(
      CHANNEL_NAME, Buffer.from(REQUEST_PAYLOAD), { timeoutMs: 2000 });

    // The responder receives the channel request and replies; the requester
    // pump collects the matching completion.
    let completion = null;
    await waitUntil(() => {
      responderPump.drain((record) => {
        if (isRequestRecord(record)) {
          assert.equal(record.parts.at(-1).data().toString(), REQUEST_PAYLOAD);
          record.reply(Buffer.from(REPLY_PAYLOAD));
        }
      });
      requesterPump.drain((record) => {
        if (record.kind === MeshRecordKind.Completion
            && operationIdEquals(record.operationId, operationId)) {
          completion = record;
        }
      });
      return completion !== null;
    }, 3000, 'channel request did not complete');

    assert.equal(completion.terminalResult, zlink.RequestResult.Ok);
    assert.equal(completion.parts[0].data().toString(), REPLY_PAYLOAD);
    console.log(`[spot/request] request: "${REQUEST_PAYLOAD}" -> reply: "${REPLY_PAYLOAD}"`);
  } finally {
    if (requesterPump) requesterPump.close();
    if (responderPump) responderPump.close();
    if (requester) requester.close();
    requesterNode.close();
    responderNode.close();
    requesterCtx.close();
    responderCtx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
