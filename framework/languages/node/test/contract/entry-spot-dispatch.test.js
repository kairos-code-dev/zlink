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
      context.reply.metadata('reply-trace-id', 'reply:player-1');
      context.reply.compress();
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
    actorResponseSender: async (targetActor, packetName, requestSeq, payload, replyOptions) => {
      response = {
        actorId: targetActor.actorId,
        packetName,
        requestSeq,
        payload,
        metadata: [...replyOptions.metadata.entries()],
        compressPayload: replyOptions.compressPayload
      };
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
    payload: { value: 'pong' },
    metadata: [['reply-trace-id', 'reply:player-1']],
    compressPayload: true
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
    },
    actorPacketTargetProvider: (actorId) => {
      assert.equal(actorId, 'player-1');
      return {
        routerChannelId: 'bingo.room.route',
        targetNodeRid: zlink.RoutingId.from('play-node-a'),
        spotRid: zlink.RoutingId.from('room-1'),
        spotKind: framework.ZLinkSpotKind.User
      };
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
    boundSessionTargetNodeRid: 'session-node',
    boundSessionSpotRid: 'session-entry',
    header: Buffer.from(header.data()).toString('base64'),
    payload: Buffer.from(payload.data()).toString('base64')
  })));

  dispatchHandler({
    event: 2,
    routed: {
      parts: [relay],
      routingId: 'play-node',
      spotRid: 'play-entry',
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
  assert.equal(capturedTarget.routerChannelId, 'bingo.room.route');
  assert.equal(String(capturedTarget.targetNodeRid), 'session-node');
  assert.equal(String(capturedTarget.spotRid), 'session-entry');
  assert.equal(typeof capturedTarget.targetNodeRid, 'string');
  assert.equal(typeof capturedTarget.spotRid, 'string');
  await waitFor(() => replies.length === 1, 'routed actor packet reply');
  const reply = JSON.parse(Buffer.from(replies[0]).toString('utf8'));
  assert.deepEqual(reply.actorPacketTarget, {
    routerChannelId: 'bingo.room.route',
    targetNodeRid: 'play-node-a',
    targetNodeRidHex: zlink.RoutingId.from('play-node-a').toHex(),
    spotRid: 'room-1',
    spotRidHex: zlink.RoutingId.from('room-1').toHex(),
    spotKind: framework.ZLinkSpotKind.User
  });
  header.close();
  payload.close();
  relay.close();
});

test('Entry Spot routed bound session command decodes registered channel serializer', async () => {
  let dispatchHandler;
  let received;
  const contentType = 'application/x-bound-session-test';
  const serializer = {
    deserialize(payload) {
      assert.equal(Buffer.from(payload.data()).toString('utf8'), 'encoded-bound-session');
      return {
        actorId: 'player-1',
        message: { hello: 'world' },
        boundPacketName: 'Notify',
        metadata: { trace: 'yes' }
      };
    }
  };

  class EntrySpot {}

  const nativeSpot = {
    routingId: 'session-entry',
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
    nativeSpot,
    nodeRid: 'session-node',
    spotNodeName: 'session',
    messageSerializers: new Map([[contentType, serializer]]),
    routedBoundSessionReceiver: async (actorId, message, packetName, metadata) => {
      received = { actorId, message, packetName, metadata: [...metadata.entries()] };
    }
  });

  await activation.create();
  await activation.configure();
  await activation.initialize();

  const header = zlink.Message.from(Buffer.from(JSON.stringify({
    kind: 3,
    channelName: 'bingo.room.route',
    messageName: '__zlink.actor.bound_session.send',
    contentType,
    correlationId: 'corr',
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  })));
  const payload = zlink.Message.from(Buffer.from('encoded-bound-session'));

  dispatchHandler({
    event: 2,
    routed: {
      parts: [header, payload],
      routingId: 'play-node',
      spotRid: 'session-entry',
      requestSeq: null,
      reply() {
        throw new Error('send command is not replyable');
      },
      close() {}
    }
  });

  await waitFor(() => received !== undefined, 'routed bound session command');
  assert.deepEqual(received, {
    actorId: 'player-1',
    message: { hello: 'world' },
    packetName: 'Notify',
    metadata: [['trace', 'yes']]
  });
  header.close();
  payload.close();
});

test('runtime host reports joined Spot route before stale remote actor packet target', () => {
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: { useInMemoryStores: true },
      routeChannels: ['bingo.room.route']
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

  assert.deepEqual(runtime.boundSessionRelay.actorPacketTargetForState('player-2'), {
    routerChannelId: 'bingo.room.route',
    targetNodeRid: 'play-node-1',
    spotRid: 'bingo-room-1',
    spotKind: 2
  });
});

test('runtime host normalizes remote actor join bound-session route ids', async () => {
  let capturedTarget;
  const actor = { actorId: 'player-1' };
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({})
  });
  runtime.actorManager = {
    async getOrCreateActor(actorId) {
      assert.equal(actorId, 'player-1');
      return actor;
    },
    getState(actorId) {
      assert.equal(actorId, 'player-1');
      return {
        setNativeActorRef() {},
        setRemoteBoundSessionTarget(target) {
          capturedTarget = target;
        },
        setJoinedSpot() {}
      };
    }
  };
  runtime.spotManager = {
    async admitActorJoin(_spotRid, joinedActor, request, commit) {
      assert.equal(joinedActor, actor);
      assert.equal(request.data().toString(), 'join-request');
      commit({});
      return { accepted: true };
    }
  };

  const result = await runtime.boundSessionRelay.receiveRemoteActorJoin({
    packetName: '__zlink.actor.join_spot.request',
    spotRid: 'room-1',
    actorId: 'player-1',
    actorType: 'PlayerActor',
    actorNodeRid: 'play-node',
    actorGeneration: '1',
    routerChannelId: 'bingo.room.route',
    request: Buffer.from('join-request').toString('base64')
  }, {
    channelName: 'bingo.room.route',
    sourceNodeRid: 'session-node'
  });

  assert.equal(result.accepted, true);
  assert.equal(capturedTarget.routerChannelId, 'bingo.room.route');
  assert.equal(String(capturedTarget.targetNodeRid), 'session-node');
  assert.equal(String(capturedTarget.spotRid), 'session-node');
  assert.equal(typeof capturedTarget.targetNodeRid, 'string');
  assert.equal(typeof capturedTarget.spotRid, 'string');
});

test('runtime host remembers routed packet target for stream-bound actors without actor manager', async () => {
  const routedTargets = [];
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: { useInMemoryStores: true },
      routeChannels: ['bingo.room.route']
    })
  });
  runtime.routeTransport.requestRawToSpot = async (remoteAddress) => {
    routedTargets.push(`${remoteAddress.routerChannelId}:${remoteAddress.targetNodeRid}`);
    return [zlink.Message.from(Buffer.from(JSON.stringify({
      ok: true,
      response: { accepted: true },
      actorPacketTarget: {
        routerChannelId: 'bingo.room.route',
        targetNodeRid: 'play-node-1',
        spotRid: 'bingo-room-1'
      }
    })))];
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

  await runtime.boundSessionRelay.relayRemoteActorPacket(actor, header, payload);
  await runtime.boundSessionRelay.relayRemoteActorPacket(actor, { ...header, requestSeq: 2n, name: 'Submit' }, payload);

  payload.close();
  assert.deepEqual(routedTargets, ['bingo.room.route:play-node-1', 'bingo.room.route:play-node-1']);
});

test('runtime host keeps routed packet target across stream actor wrappers', async () => {
  const routedTargets = [];
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: { useInMemoryStores: true },
      routeChannels: ['bingo.room.route']
    })
  });
  runtime.routeTransport.requestRawToSpot = async (remoteAddress) => {
    routedTargets.push(`${remoteAddress.routerChannelId}:${remoteAddress.targetNodeRid}:${remoteAddress.spotRid}`);
    return [zlink.Message.from(Buffer.from(JSON.stringify({
      ok: true,
      response: { accepted: true },
      actorPacketTarget: {
        routerChannelId: 'bingo.room.route',
        targetNodeRid: 'play-node-a',
        spotRid: 'room-1',
        spotKind: framework.ZLinkSpotKind.User
      }
    })))];
  };
  runtime.streamBindingRuntime.sendLocalBoundSessionResponse = () => true;

  const actorRef = {
    nodeRid: 'play-node-b',
    actorId: 'player-2',
    generation: 1n
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

  await runtime.boundSessionRelay.relayRemoteActorPacket({ actorId: 'player-2', ref: actorRef }, header, payload);
  await runtime.boundSessionRelay.relayRemoteActorPacket(
    { actorId: 'player-2', ref: actorRef },
    { ...header, requestSeq: 2n, name: 'Submit' },
    payload
  );

  payload.close();
  assert.deepEqual(routedTargets, [
    'bingo.room.route:play-node-b:play-node-b',
    'bingo.room.route:play-node-a:room-1'
  ]);
});

test('runtime host raw actor relay reply updates actor packet target for the next request', async () => {
  const routedTargets = [];
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration({
      locations: { useInMemoryStores: true },
      routeChannels: ['bingo.room.route']
    })
  });
  runtime.spotNodeRuntime = {
    primaryNode: {
      routingId: 'bingo-session-node-b'
    }
  };
  const state = new framework.ZLinkActorRuntimeState('player-2');
  state.setRemoteActorPacketTarget({
    routerChannelId: 'bingo.room.route',
    targetNodeRid: 'bingo-play-node-b',
    spotRid: 'bingo-play-node-b',
    spotKind: framework.ZLinkSpotKind.Entry
  });
  runtime.setActorManager({
    getState(actorId) {
      assert.equal(actorId, 'player-2');
      return state;
    }
  });
  runtime.routeTransport.requestRawToSpot = async (remoteAddress) => {
    routedTargets.push(`${remoteAddress.routerChannelId}:${remoteAddress.targetNodeRid}:${remoteAddress.spotRid}`);
    return [zlink.Message.from(Buffer.from(JSON.stringify({
      ok: true,
      response: { accepted: true },
      actorPacketTarget: {
        routerChannelId: 'bingo.room.route',
        targetNodeRid: 'bingo-play-node-a',
        spotRid: 'bingo-room-1',
        spotKind: framework.ZLinkSpotKind.User
      }
    })))];
  };
  runtime.streamBindingRuntime.sendLocalBoundSessionResponse = () => true;

  const actor = {
    actorId: 'player-2',
    ref: {
      nodeRid: 'bingo-play-node-b',
      actorId: 'player-2',
      generation: 1n
    }
  };
  const header = {
    kind: protocol.ZLinkStreamMessageKind.Request,
    codec: protocol.ZLinkStreamCodec.Json,
    flags: protocol.ZLinkStreamHeaderFlags.None,
    requestSeq: 1n,
    name: 'MatchBingoReq',
    metadata: { values: new Map() }
  };
  const payload = zlink.Message.from(Buffer.from(JSON.stringify({ value: 'ping' })));

  await runtime.boundSessionRelay.relayRemoteActorPacket(actor, header, payload);
  await runtime.boundSessionRelay.relayRemoteActorPacket(
    actor,
    { ...header, requestSeq: 2n, name: 'SubmitBingoCardReq' },
    payload
  );

  payload.close();
  assert.deepEqual(routedTargets, [
    'bingo.room.route:bingo-play-node-b:bingo-play-node-b',
    'bingo.room.route:bingo-play-node-a:bingo-room-1'
  ]);
  assert.deepEqual({
    routerChannelId: state.remoteActorPacketTarget.routerChannelId,
    targetNodeRid: String(state.remoteActorPacketTarget.targetNodeRid),
    spotRid: String(state.remoteActorPacketTarget.spotRid),
    spotKind: state.remoteActorPacketTarget.spotKind
  }, {
    routerChannelId: 'bingo.room.route',
    targetNodeRid: 'bingo-play-node-a',
    spotRid: 'bingo-room-1',
    spotKind: framework.ZLinkSpotKind.User
  });
});
