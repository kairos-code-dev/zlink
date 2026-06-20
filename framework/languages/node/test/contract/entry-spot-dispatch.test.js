const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const spots = require('../../packages/framework/dist/runtime/spots');
const protocol = require('../../packages/framework/dist/runtime/streams/protocol');

async function waitFor(condition, label, timeoutMs = 1000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (condition()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  throw new Error(`${label} timed out`);
}

test('Entry Spot native actor request dispatches to registered handler and replies through actor response sender', async () => {
  const calls = [];
  let dispatchHandler;
  let response;

  class PlayerActor {
    constructor(actorId) {
      this.actorId = actorId;
    }
  }

  class EntrySpot {}

  class MatchHandler {
    async handle(spot, actor, context, request) {
      assert.equal(spot instanceof EntrySpot, true);
      assert.equal(actor.actorId, 'player-1');
      assert.equal(context.packetName, 'Match');
      assert.deepEqual(request, { value: 'ping' });
      calls.push('handler');
      return { value: 'pong' };
    }
  }

  const actor = new PlayerActor('player-1');
  const nativeSpot = {
    routingId: 'entry-rid',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    async dispose() {}
  };
  const activation = new spots.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    actorRequestHandlers: [{
      entrySpotType: EntrySpot,
      actorType: PlayerActor,
      handlerType: MatchHandler,
      packetName: 'Match'
    }],
    nativeSpot,
    nodeRid: 'node-a',
    spotNodeName: 'entry-node',
    actorResolver: (actorId) => actorId === actor.actorId ? actor : undefined,
    actorResponseSender: async (targetActor, packetName, requestSeq, payload) => {
      response = { actorId: targetActor.actorId, packetName, requestSeq, payload };
      calls.push('response');
    }
  });

  await activation.create();
  await activation.configure();
  await activation.initialize();

  const header = zlink.Message.from(Buffer.from(protocol.encodeStreamHeader({
    kind: protocol.ZLinkStreamMessageKind.Request,
    codec: protocol.ZLinkStreamCodec.Json,
    flags: protocol.ZLinkStreamHeaderFlags.None,
    requestSeq: 7n,
    name: 'Match',
    metadata: new Map()
  })));
  const payload = zlink.Message.from(Buffer.from(JSON.stringify({ value: 'ping' })));
  const parts = [
    { info: { actor }, message: header, more: true },
    { info: { actor }, message: payload, more: false }
  ];

  assert.equal(typeof dispatchHandler, 'function');
  dispatchHandler({
    event: 5,
    recvActorPart() {
      return parts.shift() ?? null;
    }
  });

  await waitFor(() => calls.includes('response'), 'Entry Spot actor response');
  assert.deepEqual(calls, ['handler', 'response']);
  assert.deepEqual(response, {
    actorId: 'player-1',
    packetName: 'Match',
    requestSeq: 7n,
    payload: { value: 'pong' }
  });
});
