const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
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
    recvRoute() {
      return false;
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

test('Entry Spot routed actor packet records source node as remote bound session target', async () => {
  let capturedTarget;
  const replies = [];
  let dispatchHandler;

  class PlayerActor {
    constructor(actorId) {
      this.actorId = actorId;
    }
  }

  class EntrySpot {}
  class MatchHandler {
    async handle(_spot, actor, _context, request) {
      assert.equal(actor.actorId, 'player-1');
      assert.deepEqual(request, { value: 'ping' });
      return { value: 'pong' };
    }
  }

  const actor = new PlayerActor('player-1');
  const nativeSpot = {
    routingId: 'play-node',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    recvRoute() {
      return false;
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
    nodeRid: 'play-node',
    spotNodeName: 'room',
    actorResolver: (actorId) => actorId === actor.actorId ? actor : undefined,
    remoteActorPacketTargetReceiver: (_actorId, target) => {
      capturedTarget = target;
    }
  });

  await activation.create();
  await activation.configure();
  await activation.initialize();

  const header = zlink.Message.from(Buffer.from(protocol.encodeStreamHeader({
    kind: protocol.ZLinkStreamMessageKind.Request,
    codec: protocol.ZLinkStreamCodec.Json,
    flags: protocol.ZLinkStreamHeaderFlags.None,
    requestSeq: 9n,
    name: 'Match',
    metadata: new Map()
  })));
  const payload = zlink.Message.from(Buffer.from(JSON.stringify({ value: 'ping' })));
  const relay = zlink.Message.from(Buffer.from(JSON.stringify({
    packetName: '__zlink.actor.packet.relay',
    actorId: 'player-1',
    routerChannelId: 'bingo.room.route',
    header: Buffer.from(header.data()).toString('base64'),
    payload: Buffer.from(payload.data()).toString('base64')
  })));

  dispatchHandler({
    event: 2,
    routed: {
      parts: [relay],
      routingId: 'session-node',
      spotRid: 'session-node',
      requestSeq: 1n,
      reply() {
        return {
          message(message) {
            replies.push(message);
            return this;
          },
          submit() {}
        };
      },
      close() {}
    }
  });

  await waitFor(() => capturedTarget !== undefined, 'remote target capture');
  assert.deepEqual(capturedTarget, {
    routerChannelId: 'bingo.room.route',
    targetNodeRid: 'session-node',
    spotRid: 'session-node'
  });
  await waitFor(() => replies.length === 1, 'routed actor packet reply');
  header.close();
  payload.close();
  relay.close();
});

test('runtime host reports joined Spot route before stale remote actor packet target', () => {
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      discovery: { registries: ['tcp://127.0.0.1:19000'] },
      routeChannels: ['bingo.room.route'],
      registrySpotRemoteAddresses: {
        namespace: 'bingo',
        routerChannelId: 'bingo.room.route'
      }
    })
  });
  runtime.actorManager = {
    getState(actorId) {
      assert.equal(actorId, 'player-2');
      return {
        spotRid: 'bingo-room-1',
        nativeActorRef: {
          nodeRid: 'play-node-1',
          actorId: 'player-2',
          generation: 1n
        },
        remoteActorPacketTarget: {
          routerChannelId: 'bingo.room.route',
          targetNodeRid: 'play-node-1',
          spotRid: 'play-entry-spot'
        }
      };
    }
  };

  assert.deepEqual(runtime.actorPacketTargetForState('player-2'), {
    routerChannelId: 'bingo.room.route',
    targetNodeRid: 'play-node-1',
    spotRid: 'bingo-room-1'
  });
});

test('runtime host remembers routed packet target for stream-bound actors without actor manager', async () => {
  const routedTargets = [];
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      discovery: { registries: ['tcp://127.0.0.1:19000'] },
      routeChannels: ['bingo.room.route'],
      registrySpotRemoteAddresses: {
        namespace: 'bingo',
        routerChannelId: 'bingo.room.route'
      }
    })
  });
  runtime.routeTransport.request = async (routerChannelId, targetNodeRid) => {
    routedTargets.push(`${routerChannelId}:${targetNodeRid}`);
    return {
      ok: true,
      response: { accepted: true },
      actorPacketTarget: {
        routerChannelId: 'bingo.room.route',
        targetNodeRid: 'play-node-1',
        spotRid: 'bingo-room-1'
      }
    };
  };
  runtime.streamBindingRuntime.sendLocalBoundSessionResponse = () => true;

  const actor = {
    actorId: 'player-2',
    ref: {
      nodeRid: 'play-node-1',
      actorId: 'player-2',
      generation: 1n
    }
  };
  const header = {
    kind: protocol.ZLinkStreamMessageKind.Request,
    codec: protocol.ZLinkStreamCodec.Json,
    flags: protocol.ZLinkStreamHeaderFlags.None,
    requestSeq: 1n,
    name: 'Match',
    metadata: { values: new Map() }
  };
  const payload = zlink.Message.from(Buffer.from(JSON.stringify({ value: 'ping' })));

  await runtime.relayRemoteActorPacket(actor, header, payload);
  await runtime.relayRemoteActorPacket(actor, { ...header, requestSeq: 2n, name: 'Submit' }, payload);

  payload.close();
  assert.deepEqual(routedTargets, ['bingo.room.route:play-node-1', 'bingo.room.route:play-node-1']);
});
