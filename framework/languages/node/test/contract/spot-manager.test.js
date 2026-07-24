const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const {
  ZLinkSpotRoutedActorAdmission
} = require('../../packages/framework/dist/runtime/spots/spot-routed-actor-admission');
const {
  ZLinkSpotActorMembership
} = require('../../packages/framework/dist/runtime/spots/spot-actor-membership');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const channelProtocol = require('../../packages/framework/dist/runtime/channels/channel-envelope');
const connector = require('../../packages/stream-connector/dist');
const json = connector;
const msgpack = require('../../packages/framework-codec-msgpack/dist/server/framework.cjs');
const protobuf = require('../../packages/framework-codec-protobuf/dist/server/framework.cjs');
const flowContext = require('../../packages/framework/dist/runtime/diagnostics/flow-context');
const {
  ZLinkRoutedSpotPacketDispatch
} = require('../../packages/framework/dist/runtime/spots/spot-routed-spot-packet-dispatch');

test('local SPOT request failure rejects the caller and reports FailCaller', async () => {
  const events = [];
  class DispatchObserver {
    onMessageFlow(event) {
      events.push(event);
    }
  }
  const dispatcher = new ZLinkRoutedSpotPacketDispatch({
    resolveActivation: () => undefined,
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  await assert.rejects(
    () => dispatcher.request('missing-spot', 'MissingReq', {}, { channelName: 'local' }),
    /not active/
  );
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(events.length, 1);
  assert.equal(events[0].surface, 'spot');
  assert.equal(events[0].messageKind, 'request');
  assert.equal(events[0].reason, 'no_handler');
  assert.equal(events[0].action, 'fail_caller');
});

function customTextSerializer(prefix = 'custom:') {
  return {
    serialize(value) {
      const text = typeof value === 'string' ? value : value.text;
      return zlink.Message.from(`${prefix}${text}`);
    },
    deserialize(message) {
      const text = message.getString('utf8');
      return { text: text.startsWith(prefix) ? text.slice(prefix.length) : text };
    }
  };
}

function createActorRequestParts(packetName, payload, requestSeq = 1n) {
  return [
    zlink.Message.from(Buffer.from(streamProtocol.encodeStreamHeader({
      kind: streamProtocol.ZLinkStreamMessageKind.Request,
      codec: streamProtocol.ZLinkStreamCodec.Json,
      flags: streamProtocol.ZLinkStreamHeaderFlags.None,
      requestSeq,
      name: packetName,
      metadata: new Map()
    }))),
    zlink.Message.from(Buffer.from(JSON.stringify(payload)))
  ];
}

function createActorSendParts(packetName, payload) {
  return [
    zlink.Message.from(Buffer.from(streamProtocol.encodeStreamHeader({
      kind: streamProtocol.ZLinkStreamMessageKind.Send,
      codec: streamProtocol.ZLinkStreamCodec.Json,
      flags: streamProtocol.ZLinkStreamHeaderFlags.None,
      name: packetName,
      metadata: new Map()
    }))),
    zlink.Message.from(Buffer.from(JSON.stringify(payload)))
  ];
}

function noBindInfo(requestId = 42n, flags = 1) {
  return {
    actor: { nodeRid: zlink.RoutingId.from('node-a'), actorId: 'actor-1', generation: 1n },
    sourceNodeRid: zlink.RoutingId.from('source-node'),
    sourceSessionRid: zlink.RoutingId.from('source-session'),
    requestId,
    flags
  };
}

function decodeActorReplyFrame(message) {
  const frame = message.data();
  const headerSize = frame.readUInt16BE(0);
  const payloadSize = frame.readUInt32BE(2);
  const header = streamProtocol.decodeStreamHeader(frame.subarray(6, 6 + headerSize));
  const payload = JSON.parse(frame.subarray(6 + headerSize, 6 + headerSize + payloadSize).toString('utf8'));
  return { header, payload };
}

test('Mesh actor ingress uses the current runtime owner for handoff capture', async () => {
  let fallbackActorRef;
  const currentSpotRid = zlink.RoutingId.from('current-spot');
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [],
    actorDispatchOwnerResolver: () => ({
      actorRef: {
        nodeRid: 'current-node',
        actorId: 'actor-1',
        generation: 3n
      },
      spotRid: currentSpotRid
    }),
    async dispatchEntryActorPacket(_actorId, _parts, _returnResponse, _boundTarget, fallback) {
      fallbackActorRef = fallback;
    }
  });

  await manager.dispatchMeshActor('test.mesh',
    {
      spotRid: null,
      actor: {
        nodeRid: zlink.RoutingId.from('stale-node'),
        actorId: 'actor-1',
        generation: 1n
      }
    },
    {
      kind: zlink.ReceiveKind.ActorSend,
      parts: []
    }
  );

  assert.equal(fallbackActorRef.nodeRid, 'current-node');
  assert.equal(fallbackActorRef.generation, 3n);
  assert.equal(fallbackActorRef.handoffForwarded, true);
  assert.equal(fallbackActorRef.handoffTargetSpotRid, String(currentSpotRid));
});

test('Mesh actor ingress routes the concrete Entry Spot RID to Entry Spot actor dispatch', async () => {
  const entryNodeRid = zlink.RoutingId.from('entry-node');
  let dispatched = false;
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [],
    entryNodeRidProvider: () => entryNodeRid,
    async dispatchEntryActorPacket(actorId, parts, returnResponse, _boundTarget, fallback) {
      dispatched = true;
      assert.equal(actorId, 'actor-1');
      assert.deepEqual(parts, []);
      assert.equal(returnResponse, false);
      assert.equal(fallback.actorId, 'actor-1');
    }
  });

  await manager.dispatchMeshActor('test.mesh',
    {
      spotRid: entryNodeRid,
      actor: {
        nodeRid: entryNodeRid,
        actorId: 'actor-1',
        generation: 1n
      }
    },
    {
      kind: zlink.ReceiveKind.ActorSend,
      parts: []
    }
  );

  assert.equal(dispatched, true);
});

test('Mesh actor ingress records the validated bound-session generation before dispatch', async () => {
  const entryNodeRid = zlink.RoutingId.from('entry-node');
  const observed = [];
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [],
    entryNodeRidProvider: () => entryNodeRid,
    actorBindingGenerationObserver(actorId, generation) {
      observed.push({ actorId, generation });
    },
    async dispatchEntryActorPacket() {
      assert.deepEqual(observed, [{ actorId: 'actor-1', generation: 23n }]);
    }
  });

  await manager.dispatchMeshActor('test.mesh',
    {
      spotRid: entryNodeRid,
      actor: {
        nodeRid: entryNodeRid,
        actorId: 'actor-1',
        generation: 1n
      }
    },
    {
      kind: zlink.ReceiveKind.ActorSend,
      sourceBindingGeneration: 23n,
      parts: []
    }
  );

  assert.deepEqual(observed, [{ actorId: 'actor-1', generation: 23n }]);
});

test('spot actor leave rejoins the actor original remote Entry Spot', async () => {
  const events = [];
  const localNodeRid = zlink.RoutingId.from('play-node-a');
  const remoteEntryNodeRid = zlink.RoutingId.from('play-node-b');
  const actor = {
    actorId: 'player-2',
    context: {
      actorRef: {
        nodeRid: remoteEntryNodeRid,
        actorId: 'player-2',
        generation: 1n
      },
      joinEntrySpot(nodeRid, request) {
        events.push(['join-entry', String(nodeRid), request]);
        return {
          async submit() {
            // The remote two-phase source transfer owns the user Spot leave.
            await activation.spot.onLeaveActor(actor);
            activation.commitActorDeparture(actor.actorId);
            events.push(['submitted']);
            return { status: 'accepted' };
          }
        };
      }
    }
  };
  const activation = {
    serial: { execute: (operation) => operation() },
    beginActorTransfer: (actorId) => events.push(['begin', actorId]),
    spot: { onLeaveActor: async (left) => events.push(['leave', left.actorId]) },
    commitActorDeparture: (actorId) => events.push(['commit', actorId])
  };
  const membership = new ZLinkSpotActorMembership({
    resolveActivation: () => activation,
    entryNodeRidProvider: () => localNodeRid,
    actorTransferRuntime: {
      clearRoutedActor: (left) => events.push(['clear', left.actorId]),
      actorEntryNodeRid: () => remoteEntryNodeRid
    }
  });

  await membership.leaveActor('bingo-room', actor);

  assert.deepEqual(events.map((entry) => entry.slice(0, 2)), [
    ['join-entry', 'play-node-b'],
    ['leave', 'player-2'],
    ['commit', 'player-2'],
    ['submitted']
  ]);
});

test('ZLinkSpotManager creates lists finds and closes spots with lifecycle order', async () => {
  const events = [];
  class StageSpot {
    configure() {
      events.push('configure');
    }
    async onCreate(request) {
      events.push(`onCreate:${request.decode()}`);
      return { accepted: true };
    }
    async onInitialize() {
      events.push('onInitialize');
    }
    async onClosing(context) {
      events.push(`onClosing:${context.reason}`);
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create('test.mesh', StageSpot, 'open');
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.equal(typeof created.spotRid, 'string');
  assert.deepEqual(events, ['configure', 'onCreate:open', 'onInitialize']);
  assert.deepEqual(await manager.find('test.mesh', created.spotRid), { spotRid: created.spotRid });
  assert.deepEqual(await manager.list('test.mesh'), [{ spotRid: created.spotRid }]);
  assert.equal(await manager.close('test.mesh', created.spotRid), true);
  assert.equal(await manager.close('test.mesh', created.spotRid), false);
  assert.equal(await manager.find('test.mesh', created.spotRid), null);
  assert.deepEqual(events, [
    'configure',
    'onCreate:open',
    'onInitialize',
    `onClosing:${framework.ZLinkSpotCloseReason.ExplicitClose}`
  ]);
});

test('ZLinkSpotManager consumes MeshNode Spot send and request records', async () => {
  const handled = [];
  class MeshSpot {}
  class MeshSpotPacket {
    handle(_spot, message, context) {
      handled.push({ message, context });
      return { echoed: message.value };
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [MeshSpot],
    spotPacketHandlers: [{
      spotType: MeshSpot,
      handlerType: MeshSpotPacket,
      packetName: 'MeshSpotPacket'
    }]
  });
  const created = await manager.create('test.mesh', MeshSpot);
  const owner = {
    ownerKind: zlink.ReadyOwnerKind.Spot,
    spotRid: created.spotRid
  };
  const sendParts = channelProtocol.encodeChannelEnvelopeParts(
    3,
    'mesh',
    'MeshSpotPacket',
    { value: 'sent' }
  ).map((part) => zlink.Message.from(part));
  const requestParts = channelProtocol.encodeChannelEnvelopeParts(
    1,
    'mesh',
    'MeshSpotPacket',
    { value: 'asked' }
  ).map((part) => zlink.Message.from(part));
  let replyParts;

  try {
    await manager.dispatchMeshSpot('test.mesh', owner, {
      kind: zlink.ReceiveKind.SpotSend,
      parts: sendParts
    });
    await manager.dispatchMeshSpot('test.mesh', owner, {
      kind: zlink.ReceiveKind.SpotRequest,
      parts: requestParts,
      reply(parts) {
        replyParts = parts.map((part) => zlink.Message.from(part));
        return zlink.SubmitResult.Ok;
      }
    });

    assert.deepEqual(handled.map((entry) => entry.message), [
      { value: 'sent' },
      { value: 'asked' }
    ]);
    const reply = channelProtocol.decodeChannelEnvelope(replyParts);
    assert.deepEqual(JSON.parse(reply.payload.toString()), { echoed: 'asked' });
  } finally {
    for (const part of sendParts) part.close();
    for (const part of requestParts) part.close();
    for (const part of replyParts ?? []) part.close();
    await manager.close('test.mesh', created.spotRid);
  }
});

test('ZLinkSpotManager drain closes every local Spot in the selected mesh', async () => {
  const closed = [];
  class NaturalSpot {
    async onClosing() { closed.push('natural'); }
  }
  class RecreatedSpot {
    async onClosing() { closed.push('recreated'); }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [NaturalSpot, RecreatedSpot]
  });
  const natural = await manager.create('test.mesh', NaturalSpot);
  const recreated = await manager.create('test.mesh', RecreatedSpot);

  let completed = false;
  const draining = manager.drainForShutdown('test.mesh').then(() => { completed = true; });
  await draining;
  assert.equal(completed, true);
  assert.equal(await manager.find('test.mesh', recreated.spotRid), null);
  assert.equal(await manager.find('test.mesh', natural.spotRid), null);
  assert.deepEqual(closed.sort(), ['natural', 'recreated']);
});

test('ZLinkSpotManager reports HostShutdown only for shutdown-drained User and Instance Spots', async () => {
  const reasons = new Map();
  const record = (name) => async (context) => {
    reasons.set(name, context.reason);
  };
  class ExplicitUserSpot {
    onClosing = record('explicit-user');
  }
  class ShutdownUserSpot {
    onClosing = record('shutdown-user');
  }
  class ExplicitInstanceSpot {
    onClosing = record('explicit-instance');
  }
  class ShutdownInstanceSpot {
    onClosing = record('shutdown-instance');
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [ExplicitUserSpot, ShutdownUserSpot],
    instanceSpotFactories: new Map([[
      'test.mesh',
      new Map([
        ['explicit-instance', ExplicitInstanceSpot],
        ['shutdown-instance', ShutdownInstanceSpot]
      ])
    ]])
  });
  const explicitUser = await manager.create('test.mesh', ExplicitUserSpot);
  await manager.create('test.mesh', ShutdownUserSpot);
  const explicitInstanceRid = zlink.RoutingId.from('explicit-instance-rid');
  const shutdownInstanceRid = zlink.RoutingId.from('shutdown-instance-rid');
  await manager.materializeInstance(
    'test.mesh',
    'explicit-instance',
    explicitInstanceRid
  );
  await manager.materializeInstance(
    'test.mesh',
    'shutdown-instance',
    shutdownInstanceRid
  );

  assert.equal(await manager.close('test.mesh', explicitUser.spotRid), true);
  assert.equal(await manager.close('test.mesh', explicitInstanceRid), true);
  await manager.drainForShutdown('test.mesh');

  assert.equal(
    reasons.get('explicit-user'),
    framework.ZLinkSpotCloseReason.ExplicitClose
  );
  assert.equal(
    reasons.get('explicit-instance'),
    framework.ZLinkSpotCloseReason.ExplicitClose
  );
  assert.equal(
    reasons.get('shutdown-user'),
    framework.ZLinkSpotCloseReason.HostShutdown
  );
  assert.equal(
    reasons.get('shutdown-instance'),
    framework.ZLinkSpotCloseReason.HostShutdown
  );
});

test('ZLinkSpotManager shares concurrent close and finishes cleanup after onClosing failure', async () => {
  const entered = createDeferred();
  const release = createDeferred();
  let closingCalls = 0;
  let nativeDisposes = 0;
  class FailingCloseSpot {
    async onClosing() {
      closingCalls++;
      entered.resolve();
      await release.promise;
      throw new Error('close lifecycle failed');
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [FailingCloseSpot],
    createNativeSpot: () => ({
      routingId: 'failing-close-room',
      setDispatchHandler() {},
      setSubscription() {},
      subscribe() { return false; },
      recvActorLifecycle() { return null; },
      drainReply() { return 0; },
      drainChannelReply() { return 0; },
      recvRoute() { return false; },
      onSendReady() {},
      async dispose() { nativeDisposes++; }
    })
  });
  await manager.getOrCreate('test.mesh', FailingCloseSpot, 'failing-close-room');

  const first = assert.rejects(() => manager.close('test.mesh', 'failing-close-room'), /close lifecycle failed/);
  await entered.promise;
  const second = assert.rejects(() => manager.close('test.mesh', 'failing-close-room'), /close lifecycle failed/);
  assert.equal(await manager.find('test.mesh', 'failing-close-room'), null);
  release.resolve();
  await Promise.all([first, second]);

  assert.equal(closingCalls, 1);
  assert.equal(nativeDisposes, 1);
  assert.equal(await manager.find('test.mesh', 'failing-close-room'), null);
  assert.equal(await manager.close('test.mesh', 'failing-close-room'), false);
  assert.equal(await manager.find('test.mesh', 'failing-close-room'), null);
});

test('ZLinkSpotManager claims location before activation and releases on close', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const nodeA = await locationLifecycleNode(store, 'owner-a', 'node-a');
  const nodeB = await locationLifecycleNode(store, 'owner-b', 'node-b');
  let activatedA = 0;
  let constructedB = 0;
  let activatedB = 0;

  class StageSpot {
    async onCreate() {
      activatedA++;
      const row = await store.resolveSpot({ meshName: 'play', spotRid: rid('room-1') });
      assert.equal(row.ownerId, 'owner-a');
      return { accepted: true };
    }
  }

  class LosingSpot {
    constructor() {
      constructedB++;
    }

    onCreate() {
      activatedB++;
      return { accepted: true };
    }
  }

  const managerA = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    nodeRid: rid('node-a'),
    nodeGenerationProvider: () => 1n,
    locationLifecycle: nodeA.lifecycle,
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid, 11n)
  });
  const managerB = new framework.DefaultZLinkSpotManager({
    spotFactories: [LosingSpot],
    nodeRid: rid('node-b'),
    nodeGenerationProvider: () => 1n,
    locationLifecycle: nodeB.lifecycle,
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid, 12n)
  });

  const created = await managerA.getOrCreate('play', StageSpot, rid('room-1'));
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);

  const loser = await managerB.getOrCreate('play', LosingSpot, rid('room-1'));
  assert.equal(loser.state, framework.ZLinkSpotCreateState.Existing);
  assert.equal(activatedA, 1);
  assert.equal(constructedB, 0);
  assert.equal(activatedB, 0);

  await managerA.close('play', rid('room-1'));
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('room-1') }), undefined);
});

test('ZLinkSpotManager scopes identical Spot RIDs and Core Spot creation by MeshName', async () => {
  class StageSpot {
    onCreate() {
      return { accepted: true };
    }
  }

  const createdNativeSpots = [];
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (meshName, spotRid) => {
      createdNativeSpots.push(`${meshName}:${spotRid}`);
      return formalNativeSpot(spotRid);
    }
  });

  assert.equal(
    (await manager.getOrCreate('mesh.a', StageSpot, 'shared-room')).state,
    framework.ZLinkSpotCreateState.Created
  );
  assert.equal(
    (await manager.getOrCreate('mesh.b', StageSpot, 'shared-room')).state,
    framework.ZLinkSpotCreateState.Created
  );
  assert.deepEqual(createdNativeSpots, ['mesh.a:shared-room', 'mesh.b:shared-room']);
  assert.deepEqual(await manager.list('mesh.a'), [{ spotRid: 'shared-room' }]);
  assert.deepEqual(await manager.list('mesh.b'), [{ spotRid: 'shared-room' }]);

  assert.equal(await manager.close('mesh.a', 'shared-room'), true);
  assert.equal(await manager.find('mesh.a', 'shared-room'), null);
  assert.deepEqual(await manager.find('mesh.b', 'shared-room'), { spotRid: 'shared-room' });
  assert.equal(await manager.close('mesh.b', 'shared-room'), true);
});

test('ZLinkSpotManager rolls location claim back when activation fails or rejects', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await locationLifecycleNode(store, 'owner-a', 'node-a');

  class FailingSpot {
    async onCreate() {
      throw new Error('spot activation failed');
    }
  }
  class RejectingSpot {
    async onCreate() {
      return { accepted: false };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [FailingSpot, RejectingSpot],
    nodeRid: rid('node-a'),
    nodeGenerationProvider: () => 1n,
    locationLifecycle: node.lifecycle,
    locationMeshName: 'play',
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid, 13n)
  });

  await assert.rejects(
    () => manager.getOrCreate('test.mesh', FailingSpot, rid('fail-room')),
    /spot activation failed/
  );
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('fail-room') }), undefined);

  const rejected = await manager.getOrCreate('test.mesh', RejectingSpot, rid('reject-room'));
  assert.equal(rejected.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('reject-room') }), undefined);
});

test('ZLinkSpotManager passes dotnet-shaped context into spot constructor', async () => {
  let capturedContext;
  class StageSpot {
    constructor(context) {
      capturedContext = context;
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    nodeRid: 'node-a'
  });
  const created = await manager.getOrCreate('test.mesh', StageSpot, 'stage-a');

  assert.equal(capturedContext.spotRid, 'stage-a');
  assert.equal(capturedContext.routingId, 'stage-a');
  assert.equal(capturedContext.nodeRid, 'node-a');
  assert.equal(typeof capturedContext.addTimer, 'function');
  assert.equal(typeof capturedContext.close, 'function');
  assert.equal(typeof capturedContext.handlers.addPacket, 'function');
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
});

test('ZLinkSpotManager resolves context nodeRid lazily from provider', async () => {
  let nodeRid = 'node-before-start';
  let capturedContext;
  class StageSpot {
    constructor(context) {
      capturedContext = context;
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    nodeRidProvider: () => nodeRid
  });
  const created = await manager.getOrCreate('test.mesh', StageSpot, 'stage-lazy-node');

  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.equal(capturedContext.nodeRid, 'node-before-start');
  nodeRid = 'node-after-start';
  assert.equal(capturedContext.nodeRid, 'node-after-start');
});

test('ZLinkSpotManager passes empty framework message to onCreate without payload', async () => {
  const requests = [];
  class StageSpot {
    async onCreate(request) {
      requests.push(request.decode());
      return { accepted: true };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create('test.mesh', StageSpot);

  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.equal(requests.length, 1);
  assert.equal(requests[0], undefined);
});

test('ZLinkSpotManager create request DTOs can be decoded with framework messages', async () => {
  const payloads = [
    { kind: 'json', ready: true },
    { kind: 'messagepack', ready: true },
    { kind: 'protobuf', ready: true }
  ];
  const decoded = [];
  class CodecSpot {
    async onCreate(request) {
      decoded.push(request.decode());
      return { accepted: true };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [CodecSpot] });
  for (const payload of payloads) {
    const created = await manager.create('test.mesh', CodecSpot, payload);
    assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  }

  assert.deepEqual(decoded, [
    { kind: 'json', ready: true },
    { kind: 'messagepack', ready: true },
    { kind: 'protobuf', ready: true }
  ]);
  assert.equal(connector.ZlinkStreamCodec.Json, json.toJson({}).codec);
});

test('ZLinkSpotManager create request uses configured custom serializer without raw request code', async () => {
  const decoded = [];
  class CodecSpot {
    async onCreate(request) {
      decoded.push(request.decode());
      return { accepted: true, reply: { text: 'created' } };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [CodecSpot],
    messageSerializers: new Map([['application/x-custom-text', customTextSerializer()]])
  });

  const created = await manager.create('test.mesh', CodecSpot, { text: 'open' });

  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.deepEqual(decoded, [{ text: 'open' }]);
  assert.deepEqual(created.reply, { text: 'created' });
});

test('ZLinkSpotManager create request uses binary codec extensions without raw request code', async () => {
  const cases = [
    ['messagepack', msgpack.createMessagePackSerializer()],
    ['protobuf', protobuf.createProtobufMessageSerializer()]
  ];

  for (const [name, serializer] of cases) {
    const decoded = [];
    class CodecSpot {
      async onCreate(request) {
        decoded.push(request.decode());
        return { accepted: true, reply: { text: `${name}:created` } };
      }
    }

    const manager = new framework.DefaultZLinkSpotManager({
      spotFactories: [CodecSpot],
      messageSerializers: new Map([[`application/x-test-${name}`, serializer]])
    });

    const created = await manager.create('test.mesh', CodecSpot, { text: `${name}:open` });

    assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
    assert.deepEqual(decoded, [{ text: `${name}:open` }]);
    assert.deepEqual(created.reply, { text: `${name}:created` });
  }
});

test('spot handler registry records packet and subscribe registrations from configure', async () => {
  class PacketHandler {}
  class SubscribeHandler {}
  let registry;
  class StageSpot {
    configure() {
      this.context.handlers.addPacket(PacketHandler, 'stage.packet');
      this.context.handlers.addSubscribe(SubscribeHandler, 'stage.channel', 'stage.updated');
      registry = this.context.handlers;
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  await manager.create('test.mesh', StageSpot);

  assert.deepEqual(registry.snapshot(), [
    { kind: 'packet', handlerType: PacketHandler, packetName: 'stage.packet' },
    {
      kind: 'subscribe',
      handlerType: SubscribeHandler,
      channelName: 'stage.channel',
      topic: 'stage.updated'
    }
  ]);
});

test('ZLinkSpotManager reports SPOT subscription dispatch errors to global observer', async () => {
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  await manager.getOrCreate('test.mesh', StageSpot, 'stage-subscription');
  await dispatchMeshSubscription(manager, 'stage-subscription', {
    topic: 'unmatched',
    routingId: 'source-node',
    parts: []
  });

  assert.equal(dispatchEvents.length, 1);
  assert.equal(dispatchEvents[0].surface, 'spot');
  assert.equal(dispatchEvents[0].messageKind, 'publish');
  assert.equal(dispatchEvents[0].outcome, 'failed');
  assert.equal(dispatchEvents[0].reason, 'invalid_frame');
  assert.equal(dispatchEvents[0].action, 'drop');
  assert.equal(dispatchEvents[0].topic, 'unmatched');
  assert.equal(dispatchEvents[0].sourceRid, 'source-node');
});

test('ZLinkSpotManager serializes formal MeshNode subscription records that arrive during dispatch', async () => {
  const events = [];
  const subscriptions = [];
  const entered = createDeferred();
  const release = createDeferred();
  class StageSpot {}
  class SubscribeHandler {
    async handle(_spot, event) {
      events.push(`event:${event.marker}`);
      if (event.marker === 'first') {
        entered.resolve();
        await release.promise;
      }
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid, 1n, subscriptions),
    spotSubscriptionHandlers: [{
      spotType: StageSpot,
      handlerType: SubscribeHandler,
      channelName: 'stage.channel',
      topic: 'stage.updated'
    }]
  });

  await manager.getOrCreate('test.mesh', StageSpot, 'stage-subscription-redrain');
  assert.deepEqual(subscriptions, [{
    channelName: 'stage.channel',
    topic: 'stage.updated'
  }]);
  const firstMessage = subscriptionMessage('stage.updated', 'first');
  const secondMessage = subscriptionMessage('stage.updated', 'second');
  try {
    const first = dispatchMeshSubscription(
      manager,
      'stage-subscription-redrain',
      firstMessage
    );
    await entered.promise;
    const second = dispatchMeshSubscription(
      manager,
      'stage-subscription-redrain',
      secondMessage
    );
    release.resolve();
    await Promise.all([first, second]);

    assert.deepEqual(events, ['event:first', 'event:second']);
  } finally {
    for (const part of [...firstMessage.parts, ...secondMessage.parts]) {
      part.close();
    }
  }
});

test('SPOT subscription dispatch preserves publisher flow origin', async () => {
  const flowEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      flowEvents.push(event);
    }
  }
  class StageSpot {}
  class SubscribeHandler { async handle() {} }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} },
      framework.ZLinkMessageFlowLogMode.KeyTransitions
    ),
    detachedTaskRunner: { runDetached(_name, callback) { void callback(); } },
    spotSubscriptionHandlers: [{
      spotType: StageSpot,
      handlerType: SubscribeHandler,
      channelName: 'stage.channel',
      topic: 'stage.updated'
    }]
  });

  await manager.getOrCreate('test.mesh', StageSpot, 'stage-subscription-flow');
  const message = flowContext.runWithFlow(
    { flowId: '019f5b07-77ef-7587-8e19-6095ff11603b', flowOrigin: 'Timer' },
    () => subscriptionMessage('stage.updated', 'tick')
  );
  try {
    await dispatchMeshSubscription(manager, 'stage-subscription-flow', message);
    await waitFor(() => flowEvents.some((event) =>
      event.outcome === 'succeeded'));

    assert.equal(flowEvents.every((event) => event.flowId === '019f5b07-77ef-7587-8e19-6095ff11603b'), true);
    assert.equal(flowEvents.every((event) => event.flowOrigin === 'Timer'), true);
  } finally {
    for (const part of message.parts) {
      part.close();
    }
  }
});

test('ZLinkSpotManager reports SPOT actor dispatch errors to global observer', async () => {
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const badPart = zlink.Message.from('bad-frame');
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  try {
    await manager.getOrCreate('test.mesh', StageSpot, 'stage-actor');
    await manager.dispatchMeshActor('test.mesh',
      meshActorOwner('stage-actor', 'actor-1'),
      {
        kind: zlink.ReceiveKind.ActorSend,
        parts: [badPart]
      }
    );

    assert.equal(dispatchEvents.length, 1);
    assert.equal(dispatchEvents[0].surface, 'actor');
    assert.equal(dispatchEvents[0].messageKind, 'send');
    assert.equal(dispatchEvents[0].outcome, 'failed');
    assert.equal(dispatchEvents[0].reason, 'invalid_frame');
    assert.equal(dispatchEvents[0].action, 'drop');
    assert.equal(dispatchEvents[0].spotRid, 'stage-actor');
    assert.equal(dispatchEvents[0].actorId, 'actor-1');
  } finally {
    badPart.close();
  }
});

test('ZLinkSpotManager replies routed actor request dispatch errors', async () => {
  const dispatchEvents = [];
  const replyParts = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const requestParts = createActorRequestParts('MissingActorPacket', { value: 'payload' }, 1n);
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  try {
    await manager.getOrCreate('test.mesh', StageSpot, 'stage-routed-actor');
    await manager.dispatchMeshActor('test.mesh',
      meshActorOwner('stage-routed-actor', 'actor-1'),
      {
        kind: zlink.ReceiveKind.ActorRequest,
        parts: requestParts,
        reply(parts) {
          replyParts.push(...parts.map((part) =>
            Buffer.from(typeof part.data === 'function' ? part.data() : part)
          ));
          return zlink.SubmitResult.Ok;
        }
      }
    );

    assert.equal(replyParts.length, 2);
    assert.equal(
      streamProtocol.decodeStreamHeader(replyParts[0]).kind,
      streamProtocol.ZLinkStreamMessageKind.Error
    );
    assert.match(JSON.parse(replyParts[1].toString()).message, /not active|not found|registered/i);
    const errorEvents = dispatchEvents.filter((event) => event.outcome === 'failed');
    assert.equal(errorEvents.length, 1);
    assert.equal(errorEvents[0].surface, 'actor');
    assert.equal(errorEvents[0].reason, 'no_handler');
    assert.equal(errorEvents[0].action, 'reply_error');
  } finally {
    for (const part of requestParts) {
      part.close();
    }
  }
});

test('ZLinkSpotManager does not bind formal Mesh actor packets as remote sessions', async () => {
  const replies = [];
  const boundSessions = [];
  const parts = createActorRequestParts('ActorAsk', { value: 'ping' }, 42n, 1);
  const nativeNode = {
    routingId: zlink.RoutingId.from('node-a'),
    bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid) {
      boundSessions.push({ actor, sourceNodeRid, sourceSessionRid });
    },
    replyActorNoBind() { throw new Error('formal MeshNode dispatch must not use legacy NO_BIND reply'); }
  };
  class ProbeActor {
    constructor() {
      this.actorId = 'actor-1';
    }
  }
  class StageSpot {}
  class ProbeRequestHandler {
    async handle(_spot, actor, _context, request) {
      assert.ok(request.value === 'ping' || request.value === 'backend');
      return { value: `${request.value}:${actor.actorId}` };
    }
  }
  class ProbeSendHandler {
    async handle(_spot, actor, _context, message) {
      assert.equal(message.value, 'backend');
      assert.equal(actor.actorId, 'actor-1');
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    nativeSpotNodeProvider: () => nativeNode,
    actorResolver: () => new ProbeActor(),
    spotActorRequestHandlers: [{
      spotType: StageSpot,
      actorType: ProbeActor,
      handlerType: ProbeRequestHandler,
      packetName: 'ActorAsk'
    }],
    spotActorSendHandlers: [{
      spotType: StageSpot,
      actorType: ProbeActor,
      handlerType: ProbeSendHandler,
      packetName: 'ActorAsk'
    }]
  });

  try {
    await manager.getOrCreate('test.mesh', StageSpot, 'stage-no-bind');
    await manager.dispatchMeshActor('test.mesh',
      meshActorOwner('stage-no-bind', 'actor-1'),
      {
        kind: zlink.ReceiveKind.ActorRequest,
        parts,
        reply(replyParts) {
          replies.push(replyParts.map((part) =>
            Buffer.from(typeof part.data === 'function' ? part.data() : part)
          ));
          return zlink.SubmitResult.Ok;
        }
      }
    );

    assert.equal(boundSessions.length, 0);
    assert.equal(replies.length, 1);
    assert.equal(
      streamProtocol.decodeStreamHeader(replies[0][0]).kind,
      streamProtocol.ZLinkStreamMessageKind.Response
    );
    assert.deepEqual(JSON.parse(replies[0][1].toString()), { value: 'ping:actor-1' });

    const backendParts = createActorSendParts('ActorAsk', { value: 'backend' });
    try {
      await manager.dispatchMeshActor('test.mesh',
        meshActorOwner('stage-no-bind', 'actor-1'),
        {
          kind: zlink.ReceiveKind.ActorSend,
          parts: backendParts
        }
      );
    } finally {
      for (const part of backendParts) {
        part.close();
      }
    }
    assert.equal(boundSessions.length, 0);
  } finally {
    for (const part of parts) {
      part.close();
    }
  }
});

test('ZLinkSpotManager replies formal Mesh actor handler exceptions as HandlerException errors', async () => {
  const dispatchEvents = [];
  const replies = [];
  const boundSessions = [];
  const parts = createActorRequestParts('ThrowAsk', { value: 'boom' }, 43n, 1);
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const nativeNode = {
    routingId: zlink.RoutingId.from('node-a'),
    bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid) {
      boundSessions.push({ actor, sourceNodeRid, sourceSessionRid });
    },
    replyActorNoBind() { throw new Error('formal MeshNode dispatch must not use legacy NO_BIND reply'); }
  };
  class ProbeActor {
    constructor() {
      this.actorId = 'actor-1';
    }
  }
  class StageSpot {}
  class ThrowRequestHandler {
    async handle() {
      throw new Error('handler boom');
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    nativeSpotNodeProvider: () => nativeNode,
    actorResolver: () => new ProbeActor(),
    spotActorRequestHandlers: [{
      spotType: StageSpot,
      actorType: ProbeActor,
      handlerType: ThrowRequestHandler,
      packetName: 'ThrowAsk'
    }],
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  try {
    await manager.getOrCreate('test.mesh', StageSpot, 'stage-no-bind-error');
    await manager.dispatchMeshActor('test.mesh',
      meshActorOwner('stage-no-bind-error', 'actor-1'),
      {
        kind: zlink.ReceiveKind.ActorRequest,
        parts,
        reply(replyParts) {
          replies.push(replyParts.map((part) =>
            Buffer.from(typeof part.data === 'function' ? part.data() : part)
          ));
          return zlink.SubmitResult.Ok;
        }
      }
    );

    assert.equal(boundSessions.length, 0);
    assert.equal(replies.length, 1);
    assert.equal(
      streamProtocol.decodeStreamHeader(replies[0][0]).kind,
      streamProtocol.ZLinkStreamMessageKind.Error
    );
    assert.deepEqual(JSON.parse(replies[0][1].toString()), {
      message: 'handler boom',
      kind: 'RequestFailed',
      isRetriable: false
    });
    const errorEvents = dispatchEvents.filter((event) => event.outcome === 'failed');
    assert.equal(errorEvents.length, 1);
    assert.equal(errorEvents[0].reason, 'handler_exception');
    assert.equal(errorEvents[0].action, 'reply_error');
  } finally {
    for (const part of parts) {
      part.close();
    }
  }
});

test('ZLinkSpotManager awaits async configure before onInitialize', async () => {
  const events = [];
  class StageSpot {
    async configure() {
      await Promise.resolve();
      events.push('configure');
    }

    async onInitialize() {
      events.push('initialize');
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  await manager.create('test.mesh', StageSpot);

  assert.deepEqual(events, ['configure', 'initialize']);
});

test('ZLinkSpotManager getOrCreate is keyed by spot type and spotRid', async () => {
  class StageSpot {}
  class OtherSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot, OtherSpot] });

  assert.deepEqual(await manager.getOrCreate('test.mesh', StageSpot, 'stage-1'), {
    spotRid: 'stage-1',
    state: framework.ZLinkSpotCreateState.Created,
    reply: undefined
  });
  assert.deepEqual(await manager.getOrCreate('test.mesh', StageSpot, 'stage-1'), {
    spotRid: 'stage-1',
    state: framework.ZLinkSpotCreateState.Existing
  });
  await assert.rejects(
    () => manager.getOrCreate('test.mesh', OtherSpot, 'stage-1'),
    (error) =>
      error instanceof framework.ZLinkFrameworkException &&
      error.kind === framework.ZLinkFrameworkErrorKind.SpotTypeMismatch
  );
});

test('ZLinkSpotManager list returns spot infos ordered by routing id', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });

  await manager.getOrCreate('test.mesh', StageSpot, 'stage-c');
  await manager.getOrCreate('test.mesh', StageSpot, 'stage-a');
  await manager.getOrCreate('test.mesh', StageSpot, 'stage-b');

  assert.deepEqual(await manager.list('test.mesh'), [
    { spotRid: 'stage-a' },
    { spotRid: 'stage-b' },
    { spotRid: 'stage-c' }
  ]);
});

test('ZLinkSpotManager concurrent getOrCreate initializes once with the first create payload', async () => {
  const payloads = [];
  const entered = createDeferred();
  const release = createDeferred();
  class StageSpot {
    async onCreate(request) {
      payloads.push(request.decode());
      entered.resolve();
      await release.promise;
      return { accepted: true };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });

  const first = manager.getOrCreate('test.mesh', StageSpot, 'payload-room', 'first-a');
  await entered.promise;
  let secondSettled = false;
  const secondResult = manager.getOrCreate('test.mesh', StageSpot, 'payload-room', 'second').finally(() => {
    secondSettled = true;
  });
  await Promise.resolve();

  assert.equal(secondSettled, false);

  release.resolve();

  const results = await Promise.all([first, secondResult]);

  assert.equal(results.filter((result) => result.state === framework.ZLinkSpotCreateState.Created).length, 1);
  assert.equal(results.filter((result) => result.state === framework.ZLinkSpotCreateState.Existing).length, 1);
  assert.deepEqual(results.map((result) => result.spotRid), ['payload-room', 'payload-room']);
  assert.deepEqual(payloads, ['first-a']);
});

test('ZLinkSpotManager caller cancellation does not cancel shared getOrCreate activation', async () => {
  class TimerHandler {
    async handle() {}
  }
  class StageSpot {}
  const controller = new AbortController();
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    spotTimerHandlers: [{
      spotType: StageSpot,
      handlerType: TimerHandler,
      name: 'heartbeat',
      periodMs: 60_000
    }]
  });

  const canceledCaller = manager.getOrCreate('test.mesh',
    StageSpot,
    'shared-cancel-room',
    controller.signal
  );
  const waitingCaller = manager.getOrCreate('test.mesh', StageSpot, 'shared-cancel-room');
  controller.abort();

  await assert.rejects(canceledCaller, /aborted/);
  const result = await waitingCaller;
  assert.equal(result.state, framework.ZLinkSpotCreateState.Existing);
  assert.deepEqual(await manager.find('test.mesh', 'shared-cancel-room'), { spotRid: 'shared-cancel-room' });
  await manager.close('test.mesh', 'shared-cancel-room');
});

test('ZLinkSpotManager reserves same-turn getOrCreate before activation yields', async () => {
  let constructed = 0;
  const release = createDeferred();
  class StageSpot {
    constructor() {
      constructed++;
    }
    async onCreate() {
      await release.promise;
      return { accepted: true };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const first = manager.getOrCreate('test.mesh', StageSpot, 'same-turn-room');
  const second = manager.getOrCreate('test.mesh', StageSpot, 'same-turn-room');
  release.resolve();

  const results = await Promise.all([first, second]);
  assert.equal(constructed, 1);
  assert.equal(results.filter((result) => result.state === framework.ZLinkSpotCreateState.Created).length, 1);
  assert.equal(results.filter((result) => result.state === framework.ZLinkSpotCreateState.Existing).length, 1);
});

test('ZLinkSpotManager concurrent getOrCreate returns rejected to waiters with independent replies', async () => {
  const payloads = [];
  const entered = createDeferred();
  const release = createDeferred();
  class RejectingConcurrentSpot {
    async onCreate(request) {
      payloads.push(request.decode());
      entered.resolve();
      await release.promise;
      return { accepted: false, reply: 'reject:first' };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [RejectingConcurrentSpot] });

  const first = manager.getOrCreate('test.mesh', RejectingConcurrentSpot, 'reject-room', 'first');
  await entered.promise;
  const second = manager.getOrCreate('test.mesh', RejectingConcurrentSpot, 'reject-room', 'second');
  release.resolve();

  const [firstResult, secondResult] = await Promise.all([first, second]);

  assert.equal(firstResult.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(secondResult.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(firstResult.reply, 'reject:first');
  assert.equal(secondResult.reply, 'reject:first');
  assert.deepEqual(payloads, ['first']);
  assert.equal(await manager.find('test.mesh', 'reject-room'), null);
});

test('ZLinkSpotManager create reject returns rejected state reply and does not register spot', async () => {
  class RejectingSpot {
    async onCreate(request) {
      return { accepted: false, reply: `reject:${request.decode()}` };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [RejectingSpot] });
  const rejected = await manager.create('test.mesh', RejectingSpot, 'closed');

  assert.equal(rejected.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(rejected.reply, 'reject:closed');
  assert.equal(await manager.find('test.mesh', rejected.spotRid), null);
  assert.deepEqual(await manager.list('test.mesh'), []);
});

test('ZLinkSpotManager rejection disposes native Spot and releases its location exactly once', async () => {
  let nativeDisposes = 0;
  let locationReleases = 0;
  const nativeSpot = {
    routingId: 'rejected-cleanup-room',
    lifecycleGeneration: 1n,
    setDispatchHandler() {},
    setSubscription() {},
    subscribe() { return false; },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() { nativeDisposes++; }
  };
  const locationLifecycle = {
    async claimSpot() { return framework.ZLinkLocationWriteStatus.Stored; },
    async releaseSpot() { locationReleases++; }
  };
  class RejectingSpot {
    async onCreate() {
      return { accepted: false };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [RejectingSpot],
    nodeRid: 'node-a',
    nodeGenerationProvider: () => 1n,
    locationMeshName: 'play',
    locationLifecycle,
    createNativeSpot: () => nativeSpot
  });
  const result = await manager.getOrCreate('test.mesh', RejectingSpot, 'rejected-cleanup-room');

  assert.equal(result.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(nativeDisposes, 1);
  assert.equal(locationReleases, 1);
  assert.equal(await manager.close('test.mesh', 'rejected-cleanup-room'), false);
  assert.equal(nativeDisposes, 1);
  assert.equal(locationReleases, 1);
});

test('ZLinkSpotManager getOrCreate can retry same spotRid after create rejection', async () => {
  let accepted = false;
  const payloads = [];
  class RetryCreateSpot {
    async onCreate(request) {
      payloads.push(request.decode());
      return accepted
        ? { accepted: true }
        : { accepted: false, reply: 'try-again' };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [RetryCreateSpot] });
  const rejected = await manager.getOrCreate('test.mesh', RetryCreateSpot, 'retry-room', 'first');
  assert.equal(rejected.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(rejected.reply, 'try-again');
  assert.equal(await manager.find('test.mesh', 'retry-room'), null);
  assert.deepEqual(await manager.list('test.mesh'), []);

  accepted = true;
  const created = await manager.getOrCreate('test.mesh', RetryCreateSpot, 'retry-room', 'second');
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.deepEqual(await manager.find('test.mesh', 'retry-room'), { spotRid: 'retry-room' });
  assert.deepEqual(payloads, ['first', 'second']);
});

test('ZLinkSpotManager getOrCreate can retry same spotRid after create lifecycle failure', async () => {
  let createThrows = true;
  let initializeThrows = true;
  const events = [];
  class FailingCreateSpot {
    async onCreate() {
      events.push('create:onCreate');
      if (createThrows) {
        createThrows = false;
        throw new Error('create failed');
      }
      return { accepted: true };
    }
  }
  class FailingInitializeSpot {
    async onCreate() {
      events.push('initialize:onCreate');
      return { accepted: true };
    }
    async onInitialize() {
      events.push('initialize:onInitialize');
      if (initializeThrows) {
        initializeThrows = false;
        throw new Error('initialize failed');
      }
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [FailingCreateSpot, FailingInitializeSpot]
  });

  await assert.rejects(
    () => manager.getOrCreate('test.mesh', FailingCreateSpot, 'create-failure-room'),
    /create failed/
  );
  assert.equal(await manager.find('test.mesh', 'create-failure-room'), null);
  const createRetry = await manager.getOrCreate('test.mesh', FailingCreateSpot, 'create-failure-room');
  assert.equal(createRetry.state, framework.ZLinkSpotCreateState.Created);

  await assert.rejects(
    () => manager.getOrCreate('test.mesh', FailingInitializeSpot, 'initialize-failure-room'),
    /initialize failed/
  );
  assert.equal(await manager.find('test.mesh', 'initialize-failure-room'), null);
  const initializeRetry = await manager.getOrCreate('test.mesh', FailingInitializeSpot, 'initialize-failure-room');
  assert.equal(initializeRetry.state, framework.ZLinkSpotCreateState.Created);

  assert.deepEqual(events, [
    'create:onCreate',
    'create:onCreate',
    'initialize:onCreate',
    'initialize:onInitialize',
    'initialize:onCreate',
    'initialize:onInitialize'
  ]);
});

test('ZLinkSpotManager rejects unregistered spot factories', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [] });

  await assert.rejects(
    () => manager.create('test.mesh', StageSpot),
    framework.ZLinkConfigurationException
  );
});

test('ZLinkSpotSerialExecutor runs spot work in submission order', async () => {
  const executor = new framework.ZLinkSpotSerialExecutor();
  const events = [];

  const first = executor.execute(async () => {
    events.push('first:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('first:end');
  });
  const second = executor.execute(async () => {
    events.push('second');
  });

  await Promise.all([first, second]);
  assert.deepEqual(events, ['first:start', 'first:end', 'second']);
});

test('spot manager local actor join awaits entry leave before commit and joined callback', async () => {
  const events = [];
  let finishLeave;
  class StageSpot {
    async onActorJoin(actor, request) {
      events.push(`join:${actor.actor.actorId}:${request.decode()}`);
      return { accepted: true, reply: 'joined' };
    }
    async onJoinedActor(actor) {
      events.push(`joined:${actor.actor.actorId}`);
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    entrySpotCallbacks: {
      onLeaveActor(actor) {
        events.push(`entry-left:${actor.actorId}`);
        return new Promise((resolve) => {
          finishLeave = resolve;
        });
      }
    }
  });
  await manager.getOrCreate('test.mesh', StageSpot, 'stage-1');
  const actor = {
    actorId: 'alice',
    context: {
      [framework.ZLINK_ACTOR_LIFECYCLE_SNAPSHOT]() {
        return {
          actorRef: { nodeRid: zlink.RoutingId.from('node-a'), actorId: 'alice', generation: 1n },
          actorType: 'player',
          membershipEpoch: 1n
        };
      }
    }
  };
  const request = zlink.Message.from('hello');
  const pending = manager.admitActorJoin('stage-1', actor, request, () => events.push('commit'));
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(events, ['join:alice:hello', 'entry-left:alice']);
  finishLeave();
  const result = await pending;

  assert.equal(result.accepted, true);
  assert.equal(JSON.parse(result.reply.getString()), 'joined');
  assert.deepEqual(events, [
    'join:alice:hello',
    'entry-left:alice',
    'commit',
    'joined:alice'
  ]);
  request.close();
  result.reply.close();
});

test('formal Entry Spot LEFT control invokes the Entry Spot lifecycle callback', async () => {
  const events = [];
  const actor = { actorId: 'alice' };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [],
    entryNodeRid: 'entry-a',
    actorResolver(actorId) {
      return actorId === actor.actorId ? actor : undefined;
    },
    entrySpotCallbacks: {
      async onLeaveActor(leftActor) {
        events.push(`entry-left:${leftActor.actorId}`);
      }
    }
  });

  await manager.dispatchMeshSpotControl('test.mesh',
    { spotRid: 'entry-a' },
    {
      kindData: {
        kind: 'actorControl',
        lifecycleKind: zlink.ActorLifecycleKind.Left,
        previousActor: { actorId: actor.actorId },
        currentActor: null,
        previousSpotRid: 'entry-a'
      }
    }
  );

  assert.deepEqual(events, ['entry-left:alice']);
});

test('user Spot join runs source leave on the caller turn without target-to-source deadlock', async () => {
  const events = [];
  class RoomSpot {
    constructor(context) {
      this.context = context;
    }
    async onActorJoin(actorId) {
      events.push(`admit:${this.context.spotRid}:${actorId}`);
      return { accepted: true };
    }
    async onLeaveActor(actor) {
      events.push(`leave:${this.context.spotRid}:${actor.actorId}`);
    }
    async onJoinedActor(actor) {
      events.push(`joined:${this.context.spotRid}:${actor.actorId}`);
    }
  }
  let manager;
  manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [RoomSpot],
    entrySpotCallbacks: {
      onLeaveActor(actor) {
        return manager.executeOnSpot(RoomSpot, actor.sourceSpotRid, (source) =>
          source.onLeaveActor(actor));
      }
    }
  });
  await manager.getOrCreate('test.mesh', RoomSpot, 'room-a');
  await manager.getOrCreate('test.mesh', RoomSpot, 'room-b');
  const actor = { actorId: 'alice', sourceSpotRid: 'room-a' };
  const request = zlink.Message.from('move');

  const move = manager.executeOnSpot(RoomSpot, 'room-a', () =>
    manager.admitActorJoin('room-b', actor, request, () => {
      events.push('commit:room-b:alice');
    }));
  const outcome = await Promise.race([
    move.then(() => 'completed'),
    new Promise((resolve) => setTimeout(() => resolve('timed-out'), 50))
  ]);

  assert.equal(outcome, 'completed');
  assert.deepEqual(events, [
    'admit:room-b:alice',
    'leave:room-a:alice',
    'commit:room-b:alice',
    'joined:room-b:alice'
  ]);
  request.close();
});

test('spot manager rolls local membership back when joined callback fails', async () => {
  const events = [];
  let committed = false;
  class StageSpot {
    async onActorJoin() {
      events.push('admission');
      return { accepted: true };
    }
    async onJoinedActor() {
      events.push('joined');
      throw new Error('joined failed');
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    entrySpotCallbacks: {
      async onLeaveActor() { events.push('entry-left'); }
    }
  });
  await manager.getOrCreate('test.mesh', StageSpot, 'stage-rollback');
  const request = zlink.Message.from('join');
  await assert.rejects(
    () => manager.admitActorJoin('stage-rollback', { actorId: 'alice' }, request, () => {
      committed = true;
      events.push('commit');
      return () => {
        committed = false;
        events.push('rollback');
      };
    }),
    /joined failed/
  );

  assert.equal(committed, false);
  assert.deepEqual(events, ['admission', 'entry-left', 'commit', 'joined', 'rollback']);
  await manager.close('test.mesh', 'stage-rollback');
  request.close();
});

test('routed actor transfer separates admission from materialization and commit', async () => {
  const events = [];
  const replies = [];
  let failJoined = false;
  let admissionGate;
  let transferInGate;
  const target = {
    async onActorJoin(actor, request) {
      events.push(`admission:${actor.actor.actorId}:${request.decode()}`);
      await admissionGate;
      return { accepted: true, reply: 'admitted' };
    },
    async onJoinedActor(actor) {
      events.push(`joined:${actor.actor.actorId}`);
      if (failJoined) {
        throw new Error('joined failed');
      }
    }
  };
  const admission = new ZLinkSpotRoutedActorAdmission({
    serial: new framework.ZLinkSpotSerialExecutor(),
    resolveActor: () => undefined,
    getTarget: () => target,
    defaultAccept: false,
    pendingAdmissionTimeoutMs: 5,
    async routedActorTransferProvider(actorId, actorType, adapterKey, state) {
      events.push(`transferIn:${actorId}:${actorType}:${adapterKey ?? 'default'}:${state.data().toString()}`);
      await transferInGate;
      return {
        actor: { actorId, value: state.data().toString() },
        actorRef: { nodeRid: zlink.RoutingId.from('target-node'), actorId, generation: 2n }
      };
    },
    async commitTransferredActor(actor) {
      events.push(`location:${actor.actorId}`);
      try {
        events.push(`commit:${actor.actorId}`);
        await target.onJoinedActor({
          actor: { nodeRid: zlink.RoutingId.from('target-node'), actorId: actor.actorId, generation: 2n },
          actorType: 'player',
          membershipEpoch: 1n
        });
        return [];
      } catch (error) {
        events.push(`rollback:${actor.actorId}`);
        throw error;
      }
    }
  });
  const makeReceived = (payload) => {
    const part = zlink.Message.from(Buffer.from(JSON.stringify(payload)));
    return {
      part,
      received: {
        requestSeq: 1n,
        parts: [part],
        routingId: null,
        spotRid: 'room-1',
        reply() {
          return {
            message(message) {
              replies.push(JSON.parse(Buffer.from(message).toString()));
              return this;
            },
            submit() {}
          };
        }
      }
    };
  };
  const common = {
    packetName: '__zlink.actor.join_spot.request',
    actorId: 'alice',
    actorType: 'player',
    actorNodeRid: 'source-node',
    actorGeneration: '1',
    expectedMembershipEpoch: '1',
    request: Buffer.from(JSON.stringify('join')).toString('base64'),
    transferId: 'transfer-1'
  };
  const prepared = makeReceived({ ...common, phase: 'admission' });
  assert.equal(await admission.admit(prepared.received), true);
  prepared.part.close();
  assert.deepEqual(events, ['admission:alice:join']);
  assert.equal(replies[0].accepted, true);
  assert.equal(Buffer.from(replies[0].reply, 'base64').toString(), JSON.stringify('admitted'));

  const duplicateAdmission = makeReceived({ ...common, phase: 'admission' });
  assert.equal(await admission.admit(duplicateAdmission.received), true);
  duplicateAdmission.part.close();
  assert.deepEqual(events, ['admission:alice:join']);
  assert.equal(replies[1].accepted, true);

  const committed = makeReceived({
    ...common,
    phase: 'commit',
    transferAdapterKey: 'PlayerActor',
    transferState: Buffer.from('state-42').toString('base64')
  });
  assert.equal(await admission.admit(committed.received), true);
  committed.part.close();
  assert.deepEqual(events, [
    'admission:alice:join',
    'transferIn:alice:player:PlayerActor:state-42',
    'location:alice',
    'commit:alice',
    'joined:alice'
  ]);
  assert.equal(replies[2].accepted, true);
  assert.equal(replies[2].actorNodeRid, 'target-node');

  const duplicateCommit = makeReceived({
    ...common,
    phase: 'commit',
    transferAdapterKey: 'PlayerActor',
    transferState: Buffer.from('state-42').toString('base64')
  });
  assert.equal(await admission.admit(duplicateCommit.received), true);
  duplicateCommit.part.close();
  assert.equal(events.filter((event) => event.startsWith('transferIn:')).length, 1);
  assert.equal(replies[3].accepted, true);

  const expiring = makeReceived({ ...common, transferId: 'transfer-expired', phase: 'admission' });
  assert.equal(await admission.admit(expiring.received), true);
  expiring.part.close();
  await new Promise((resolve) => setTimeout(resolve, 10));
  const lateCommit = makeReceived({
    ...common,
    transferId: 'transfer-expired',
    phase: 'commit',
    transferState: ''
  });
  assert.equal(await admission.admit(lateCommit.received), true);
  lateCommit.part.close();
  assert.equal(replies[5].accepted, false);
  assert.equal(events.filter((event) => event.startsWith('joined:')).length, 1);

  failJoined = true;
  const failingAdmission = makeReceived({ ...common, transferId: 'transfer-fail', phase: 'admission' });
  await admission.admit(failingAdmission.received);
  failingAdmission.part.close();
  const failingCommit = makeReceived({
    ...common,
    transferId: 'transfer-fail',
    phase: 'commit',
    transferState: Buffer.from('failed-state').toString('base64')
  });
  await admission.admit(failingCommit.received);
  failingCommit.part.close();
  assert.equal(replies[7].accepted, false);
  assert.deepEqual(events.slice(-5), [
    'transferIn:alice:player:default:failed-state',
    'location:alice',
    'commit:alice',
    'joined:alice',
    'rollback:alice'
  ]);

  failJoined = false;
  let releaseAdmission;
  admissionGate = new Promise((resolve) => { releaseAdmission = resolve; });
  const concurrentAdmissionA = makeReceived({ ...common, transferId: 'transfer-concurrent', phase: 'admission' });
  const concurrentAdmissionB = makeReceived({ ...common, transferId: 'transfer-concurrent', phase: 'admission' });
  const admissionCountBefore = events.filter((event) => event.startsWith('admission:')).length;
  const admissionA = admission.admit(concurrentAdmissionA.received);
  await new Promise((resolve) => setImmediate(resolve));
  const admissionB = admission.admit(concurrentAdmissionB.received);
  releaseAdmission();
  await Promise.all([admissionA, admissionB]);
  concurrentAdmissionA.part.close();
  concurrentAdmissionB.part.close();
  assert.equal(events.filter((event) => event.startsWith('admission:')).length, admissionCountBefore + 1);

  admissionGate = undefined;
  let releaseTransferIn;
  transferInGate = new Promise((resolve) => { releaseTransferIn = resolve; });
  const concurrentCommitPayload = {
    ...common,
    transferId: 'transfer-concurrent',
    phase: 'commit',
    transferState: Buffer.from('concurrent-state').toString('base64')
  };
  const concurrentCommitA = makeReceived(concurrentCommitPayload);
  const concurrentCommitB = makeReceived(concurrentCommitPayload);
  const transferInCountBefore = events.filter((event) => event.startsWith('transferIn:')).length;
  const commitA = admission.admit(concurrentCommitA.received);
  await new Promise((resolve) => setImmediate(resolve));
  const commitB = admission.admit(concurrentCommitB.received);
  releaseTransferIn();
  await Promise.all([commitA, commitB]);
  concurrentCommitA.part.close();
  concurrentCommitB.part.close();
  assert.equal(events.filter((event) => event.startsWith('transferIn:')).length, transferInCountBefore + 1);
});

test('spot manager rejects one-phase native remote join without materializing a target actor', async () => {
  let materialized = false;
  const replies = [];
  const nativeJoinMessage = zlink.Message.from(JSON.stringify({
    packetName: '__zlink.actor.join_spot.request',
    actorType: 'PlayerActor',
    actorNodeRid: 'play-b',
    actorGeneration: '4',
    request: Buffer.from('join-room').toString('base64')
  }));
  class RoomSpot {
    async onActorJoin() {
      return { accepted: true };
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [RoomSpot],
    createNativeSpot: (_meshName, spotRid) => formalNativeSpot(spotRid),
    actorTransferRuntime: {
      async materializeRoutedActor() {
        materialized = true;
        throw new Error('one-phase join must not materialize an actor');
      }
    }
  });

  await manager.getOrCreate('test.mesh', RoomSpot, 'room-1');
  await manager.dispatchMeshActorJoin('test.mesh',
    {
      ownerKind: zlink.ReadyOwnerKind.Spot,
      spotRid: zlink.RoutingId.from('room-1'),
      actor: null
    },
    {
      kind: zlink.ReceiveKind.SpotControl,
      kindData: {
        kind: 'actorControl',
        currentActor: {
          nodeRid: zlink.RoutingId.from('play-a'),
          actorId: 'player-2',
          generation: 5n
        }
      },
      parts: [nativeJoinMessage],
      replyActorJoin(code) {
        replies.push(code);
        return zlink.SubmitResult.Ok;
      }
    }
  );

  assert.equal(materialized, false);
  assert.equal(replies[0], 1);
  await manager.close('test.mesh', 'room-1');
  nativeJoinMessage.close();
});

test('spot outbound requestToChannel completion runs on the spot serial executor', async () => {
  const events = [];
  class StageSpot {}
  const channelClient = {
    requestToChannel(channelName, request) {
      return {
        packetName() { return this; },
        timeout() { return this; },
        async submit() {
          events.push(`request:${channelName}:${request}`);
          return 'reply';
        }
      };
    },
    sendToChannel() {
      throw new Error('not used');
    },
    request() {
      throw new Error('not used');
    },
    send() {
      throw new Error('not used');
    }
  };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    channelClient
  });
  const created = await manager.create('test.mesh', StageSpot);
  let outbound;
  await manager.executeOnSpot(StageSpot, created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  const first = manager.executeOnSpot(StageSpot, created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });
  const reply = await outbound.requestToChannel('api', 'ping').submit();
  await first;

  assert.equal(reply, 'reply');
  assert.deepEqual(events, ['spot:start', 'spot:end', 'request:api:ping']);
});

test('spot outbound routed send and request use SpotRef targets inside serial executor', async () => {
  const events = [];
  class StageSpot {}
  const targetSpotRef = {
    meshName: 'play.route',
    nodeRid: 'node-b',
    spotRid: 'stage-b',
    spotKind: framework.ZLinkSpotKind.User,
    spotGeneration: 9n
  };
  const targetSpot = framework.createSpotHandle(
    targetSpotRef.spotRid,
    async () => targetSpotRef
  );
  const routedTransport = {
    async sendToSpot(address, message, options) {
      events.push(
        `send:${address.targetNodeRid}:${address.spotRid}:${address.spotKind}:` +
        `${address.targetSpotGeneration}:${options.packetName}:${message}`
      );
    },
    async requestToSpot(address, request, options) {
      events.push(
        `request:${address.routerChannelId}:${address.spotRid}:${address.spotKind}:` +
        `${address.targetSpotGeneration}:${options.timeoutMs}:${request}`
      );
      return 'routed-reply';
    }
  };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    routedTransport
  });
  const created = await manager.create('test.mesh', StageSpot);
  let outbound;
  await manager.executeOnSpot(StageSpot, created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  const first = manager.executeOnSpot(StageSpot, created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });
  class Notice extends String {}
  class Ping extends String {}
  const send = outbound.sendToSpot(targetSpot, new Notice('notice')).submit();
  const reply = await outbound.requestToSpot(targetSpot, new Ping('ping')).timeout(250).submit();
  await send;
  await first;

  assert.equal(reply, 'routed-reply');
  assert.deepEqual(events, [
    'spot:start',
    'spot:end',
    `send:node-b:stage-b:${framework.ZLinkSpotKind.User}:9:Notice:notice`,
    `request:play.route:stage-b:${framework.ZLinkSpotKind.User}:9:250:ping`
  ]);
});

test('spot outbound routed calls require runtime transport', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create('test.mesh', StageSpot);
  let outbound;
  await manager.executeOnSpot(StageSpot, created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  assert.throws(
    () => outbound.sendToSpot({
      meshName: 'play.route',
      nodeRid: 'node-b',
      spotRid: 'stage-b'
    }, 'notice'),
    framework.ZLinkConfigurationException
  );
});

test('SpotRid outbound keeps Missing Instance placement intent behind the public call builder', async () => {
  const calls = [];
  const addressTransport = {
    async sendToSpotAddress(spotRid, message, options) {
      calls.push({ kind: 'send', spotRid, message, options });
      return { status: framework.ZLinkSubmitStatus.Accepted };
    },
    async requestToSpotAddress(spotRid, request, options) {
      calls.push({ kind: 'request', spotRid, request, options });
      return 'ready-reply';
    }
  };
  const outbound = new framework.DefaultZLinkSpotOutbound(
    new framework.ZLinkSpotSerialExecutor(),
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    'source.mesh',
    undefined,
    addressTransport
  );

  await outbound.sendToSpot('instance-42', { value: 1 })
    .metadata('trace', 'abc')
    .instanceSpot('chat-room')
    .inMesh('target.mesh')
    .submit();
  const reply = await outbound.requestToSpot('instance-42', { value: 2 })
    .instanceSpot()
    .timeout(1500)
    .submit();

  assert.equal(reply, 'ready-reply');
  assert.equal(calls[0].spotRid, 'instance-42');
  assert.equal(calls[0].options.instanceSpotType, 'chat-room');
  assert.equal(calls[0].options.initialMeshName, 'target.mesh');
  assert.equal(calls[0].options.metadata.get('trace'), 'abc');
  assert.equal(calls[1].options.instanceSpot, true);
  assert.equal(calls[1].options.instanceSpotType, undefined);
  assert.equal(calls[1].options.timeoutMs, 1500);

  const reused = outbound.sendToSpot('instance-43', 'payload').instanceSpot();
  await reused.submit();
  assert.throws(() => reused.submit(), framework.ZLinkConfigurationException);
});

test('spot timer dispatches handler on the spot serial executor with dotnet tick metadata', async () => {
  const events = [];
  let firstTick;
  const tickReceived = new Promise((resolve) => {
    firstTick = resolve;
  });
  class HeartbeatHandler {
    async handle(spot, tick) {
      events.push(`origin:${flowContext.currentOrCreateFlow().flowOrigin}`);
      events.push(`tick:${tick.deliveryIndex}:${spot.context.spotRid}`);
      firstTick(tick);
    }
  }
  class StageSpot {
    async onInitialize() {
      this.timer = await this.context.addTimer(
        'heartbeat',
        1,
        HeartbeatHandler,
        { overrunPolicy: framework.ZLinkTimerOverrunPolicy.DelayNextTick }
      );
    }
    async onClosing() {
      events.push(`closing:${this.timer.isDisposed}`);
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create('test.mesh', StageSpot);
  const blockingTurn = manager.executeOnSpot(StageSpot, created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });

  const tick = await tickReceived;
  await blockingTurn;
  await manager.close('test.mesh', created.spotRid);

  assert.equal(tick.name, 'heartbeat');
  assert.equal(tick.deliveryIndex, 1n);
  assert.equal(tick.scheduledIndex, 1n);
  assert.equal(tick.periodMs, 1);
  assert.equal(tick.skippedTicks, 0n);
  assert.equal(tick.scheduledAt instanceof Date, true);
  assert.equal(tick.startedAt instanceof Date, true);
  assert.deepEqual(events, ['spot:start', 'spot:end', 'origin:Timer', 'tick:1:spot-1', 'closing:false']);
});

test('ZLinkSpotContext close closes current spot after timer callback returns', async () => {
  const events = [];
  const closed = createDeferred();
  class SelfCloseHandler {
    async handle(spot) {
      events.push('tick');
      assert.equal(await spot.context.close(), true);
      events.push('after-close-request');
      closed.resolve();
    }
  }
  class SelfClosingSpot {
    async onInitialize() {
      await this.context.addTimer('self-close', 1, SelfCloseHandler);
    }
    async onClosing() {
      events.push('closing');
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [SelfClosingSpot] });
  const created = await manager.create('test.mesh', SelfClosingSpot);
  await closed.promise;
  await waitFor(async () => (await manager.find('test.mesh', created.spotRid)) === null);
  await waitFor(() => events.includes('closing'));

  assert.deepEqual(events, ['tick', 'after-close-request', 'closing']);
});

test('ZLinkSpotManager close rejects user spot while joined actors remain', async () => {
  let actorCount = 1;
  const events = [];
  class OccupiedSpot {
    async onClosing() {
      events.push('closing');
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [OccupiedSpot],
    actorCountProvider: () => actorCount
  });
  const created = await manager.create('test.mesh', OccupiedSpot);

  assert.equal(await manager.close('test.mesh', created.spotRid), false);
  assert.deepEqual(await manager.find('test.mesh', created.spotRid), { spotRid: created.spotRid });
  assert.deepEqual(events, []);

  actorCount = 0;
  assert.equal(await manager.close('test.mesh', created.spotRid), true);
  assert.equal(await manager.find('test.mesh', created.spotRid), null);
  assert.deepEqual(events, ['closing']);
});

test('ZLinkSpotManager close rechecks actor occupancy after earlier serial work', async () => {
  let actorCount = 0;
  const turnStarted = createDeferred();
  const releaseTurn = createDeferred();
  class OccupiedSpot {}

  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [OccupiedSpot],
    actorCountProvider: () => actorCount
  });
  const created = await manager.create('test.mesh', OccupiedSpot);
  const blockingTurn = manager.executeOnSpot(OccupiedSpot, created.spotRid, async () => {
    turnStarted.resolve();
    await releaseTurn.promise;
  });
  await turnStarted.promise;
  const actorJoin = manager.executeOnSpot(OccupiedSpot, created.spotRid, () => {
    actorCount = 1;
  });
  const closing = manager.close('test.mesh', created.spotRid);

  releaseTurn.resolve();
  await blockingTurn;
  await actorJoin;

  assert.equal(await closing, false);
  assert.deepEqual(await manager.find('test.mesh', created.spotRid), { spotRid: created.spotRid });
});

test('spot timer rejects invalid options', async () => {
  class Handler {
    async handle() {}
  }
  class StageSpot {}

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create('test.mesh', StageSpot);

  await assert.rejects(
    () => manager.executeOnSpot(StageSpot, created.spotRid, (spot) => spot.context.addTimer('', 1, Handler)),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => manager.executeOnSpot(StageSpot, created.spotRid, (spot) => spot.context.addTimer('bad-period', 0, Handler)),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => manager.executeOnSpot(StageSpot, created.spotRid, (spot) =>
      spot.context.addTimer('bad-policy', 1, Handler, { overrunPolicy: 'unsupported' })
    ),
    framework.ZLinkConfigurationException
  );
  await assert.rejects(
    () => manager.executeOnSpot(StageSpot, created.spotRid, (spot) =>
      spot.context.addTimer('bad-catchup', 1, Handler, {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.CatchUpBounded,
        maxCatchUpTicks: 0
      })
    ),
    framework.ZLinkConfigurationException
  );
});

test('spot managed timer overrun policies follow dotnet skip catch-up and fixed-delay semantics', async () => {
  await withFakeTimerClock(async (clock) => {
    const skipLateTicks = [];
    const skipLateTimer = new framework.ZLinkManagedTimer(
      'skip-late',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.SkipLateTicks,
        maxCatchUpTicks: 1,
        stopOnUnhandledException: false
      },
      async (tick) => {
        skipLateTicks.push(tick);
        if (skipLateTicks.length === 1) {
          clock.advanceBy(35);
        }
      }
    );

    await clock.runNext();
    await clock.runNext();
    await skipLateTimer.cancel();

    assert.deepEqual(skipLateTicks.map((tick) => tick.scheduledIndex), [1n, 4n]);
    assert.equal(skipLateTicks[1].skippedTicks, 2n);
  });

  await withFakeTimerClock(async (clock) => {
    const catchUpTicks = [];
    const catchUpTimer = new framework.ZLinkManagedTimer(
      'catch-up',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.CatchUpBounded,
        maxCatchUpTicks: 2,
        stopOnUnhandledException: false
      },
      async (tick) => {
        catchUpTicks.push(tick);
        if (catchUpTicks.length === 1) {
          clock.advanceBy(35);
        }
      }
    );

    await clock.runNext();
    await clock.runNext();
    await clock.runNext();
    await catchUpTimer.cancel();

    assert.deepEqual(catchUpTicks.map((tick) => tick.scheduledIndex), [1n, 3n, 4n]);
    assert.equal(catchUpTicks[1].skippedTicks, 1n);
    assert.equal(catchUpTicks[2].skippedTicks, 0n);
  });

  await withFakeTimerClock(async (clock) => {
    const delayNextTicks = [];
    const delayNextTimer = new framework.ZLinkManagedTimer(
      'delay-next',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.DelayNextTick,
        maxCatchUpTicks: 1,
        stopOnUnhandledException: false
      },
      async (tick) => {
        delayNextTicks.push(tick);
        if (delayNextTicks.length === 1) {
          clock.advanceBy(35);
        }
      }
    );

    await clock.runNext();
    assert.deepEqual(clock.pendingDelays(), [10]);
    await clock.runNext();
    await delayNextTimer.cancel();

    assert.deepEqual(delayNextTicks.map((tick) => tick.scheduledIndex), [1n, 2n]);
    assert.equal(delayNextTicks[1].skippedTicks, 0n);
  });
});

test('spot managed timer stopOnUnhandledException stops after handler failure', async () => {
  await withFakeTimerClock(async (clock) => {
    let attempts = 0;
    const timer = new framework.ZLinkManagedTimer(
      'failing',
      10,
      {
        overrunPolicy: framework.ZLinkTimerOverrunPolicy.SkipLateTicks,
        maxCatchUpTicks: 1,
        stopOnUnhandledException: true
      },
      async () => {
        attempts += 1;
        throw new Error('timer failed');
      }
    );

    await clock.runNext();

    assert.equal(attempts, 1);
    assert.equal(timer.isDisposed, true);
    assert.deepEqual(clock.pendingDelays(), []);
  });
});

test('spot timer registry replaces the same key and suppresses the queued old generation', async () => {
  await withFakeTimerClock(async (clock) => {
    const ticks = [];
    const queued = [];
    const serial = {
      isExecuting: true,
      execute(work) {
        return new Promise((resolve, reject) => queued.push({ work, resolve, reject }));
      }
    };
    class FirstHandler {
      async handle() {
        ticks.push('first');
      }
    }
    class SecondHandler {
      async handle() {
        ticks.push('second');
      }
    }
    const registry = new framework.ZLinkSpotTimerRegistry();
    const first = await registry.add('heartbeat', 10, undefined, FirstHandler, serial, {});
    await clock.runNext();
    assert.equal(queued.length, 1);

    const second = await registry.add('heartbeat', 10, undefined, SecondHandler, serial, {});
    assert.equal(first.isDisposed, true);
    const stale = queued.shift();
    await Promise.resolve(stale.work()).then(stale.resolve, stale.reject);
    assert.deepEqual(ticks, []);

    await clock.runNext();
    const current = queued.shift();
    await Promise.resolve(current.work()).then(current.resolve, current.reject);
    assert.deepEqual(ticks, ['second']);

    await second.cancel();
    assert.equal(second.isDisposed, true);
    await registry.dispose();
  });
});

async function withFakeTimerClock(run) {
  const originalNow = Date.now;
  const originalSetTimeout = global.setTimeout;
  const originalClearTimeout = global.clearTimeout;
  let now = 0;
  let nextId = 1;
  const timers = [];

  Date.now = () => now;
  global.setTimeout = (callback, delay) => {
    const timer = {
      id: nextId++,
      callback,
      delay: Number(delay) || 0,
      cleared: false
    };
    timers.push(timer);
    return timer;
  };
  global.clearTimeout = (timer) => {
    if (timer && typeof timer === 'object') {
      timer.cleared = true;
      return;
    }
    const found = timers.find((entry) => entry.id === timer);
    if (found !== undefined) {
      found.cleared = true;
    }
  };

  const clock = {
    advanceBy(ms) {
      now += ms;
    },
    async runNext() {
      const timer = timers.shift();
      assert.ok(timer, 'expected a scheduled timer callback');
      if (timer.cleared) {
        return;
      }
      now += timer.delay;
      timer.callback();
      await Promise.resolve();
      await Promise.resolve();
    },
    pendingDelays() {
      return timers
        .filter((timer) => !timer.cleared)
        .map((timer) => timer.delay);
    }
  };

  try {
    await run(clock);
  } finally {
    Date.now = originalNow;
    global.setTimeout = originalSetTimeout;
    global.clearTimeout = originalClearTimeout;
  }
}

function createDeferred() {
  let resolve;
  let reject;
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

function createLengthPrefixedJsonType() {
  return {
    encode(value) {
      const jsonBytes = new TextEncoder().encode(JSON.stringify(value));
      return {
        finish() {
          const bytes = new Uint8Array(4 + jsonBytes.length);
          bytes[0] = (jsonBytes.length >>> 24) & 0xff;
          bytes[1] = (jsonBytes.length >>> 16) & 0xff;
          bytes[2] = (jsonBytes.length >>> 8) & 0xff;
          bytes[3] = jsonBytes.length & 0xff;
          bytes.set(jsonBytes, 4);
          return bytes;
        }
      };
    },
    decode(bytes) {
      const length = (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
      return JSON.parse(Buffer.from(bytes.slice(4, 4 + length)).toString());
    },
    toObject(value) {
      return value;
    }
  };
}

async function locationLifecycleNode(store, ownerId, nodeRid) {
  const runtime = new framework.ZLinkLocationRuntime({
    stores: {
      locationStore: store,
      peerStore: store,
      spotStore: store,
      actorStore: store,
      routeStore: store,
      ownerLeaseStore: store
    },
    ownerId,
    now: () => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)),
    setTimer() {
      return 0;
    },
    clearTimer() {}
  });
  await runtime.start(rid(nodeRid));
  return {
    runtime,
    lifecycle: new framework.ZLinkLocationLifecycle(runtime, store)
  };
}

function rid(value) {
  return zlink.RoutingId.from(value);
}

function formalNativeSpot(spotRid, lifecycleGeneration = 1n, subscriptions = []) {
  return {
    routingId: spotRid,
    lifecycleGeneration,
    setDispatchHandler() {},
    setSubscription(channelName, topic) { subscriptions.push({ channelName, topic }); },
    subscribe() { return false; },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
}

function meshActorOwner(spotRid, actorId) {
  return {
    ownerKind: zlink.ReadyOwnerKind.Actor,
    spotRid: zlink.RoutingId.from(spotRid),
    actor: {
      nodeRid: zlink.RoutingId.from('node-a'),
      actorId,
      generation: 1n
    }
  };
}

async function dispatchMeshSubscription(manager, spotRid, message) {
  await manager.dispatchMeshSpot('test.mesh',
    {
      ownerKind: zlink.ReadyOwnerKind.Spot,
      spotRid: zlink.RoutingId.from(spotRid),
      actor: null
    },
    {
      kind: zlink.ReceiveKind.SpotMulticast,
      topic: message.topic,
      sourceNodeRid: message.routingId === null
        ? null
        : zlink.RoutingId.from(message.routingId),
      parts: message.parts
    }
  );
}

function subscriptionMessage(topic, marker, channelName = 'stage.channel') {
  return {
    topic,
    routingId: 'publisher',
    parts: channelProtocol.encodeChannelPublishEnvelopeParts(
      channelName,
      topic,
      'SpotMsg',
      { marker }
    ).map((part) => zlink.Message.from(part))
  };
}

async function waitFor(predicate, timeoutMs = 1000, message) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.fail(message?.() ?? 'timed out waiting for condition');
}

function dispatchErrorReporter(observerType, sink, mode = framework.ZLinkMessageFlowLogMode.ErrorsOnly) {
  return new framework.ZLinkDispatchErrorReporter(
    undefined,
    undefined,
    sink,
    {
      diagnostics: { sampleRate: 1 },
      liveMode: { mode },
      messageFlowObserverType: observerType
    }
  );
}
