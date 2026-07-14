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
  assert.equal(events[0].surface, framework.ZLinkDispatchErrorSurface.SpotRoute);
  assert.equal(events[0].messageKind, framework.ZLinkDispatchMessageKind.Request);
  assert.equal(events[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerMissing);
  assert.equal(events[0].errorAction, framework.ZLinkDispatchErrorAction.FailCaller);
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
    async onClosing() {
      events.push('onClosing');
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot, 'open');
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.equal(typeof created.spotRid, 'string');
  assert.deepEqual(events, ['configure', 'onCreate:open', 'onInitialize']);
  assert.deepEqual(await manager.find(created.spotRid), { spotRid: created.spotRid });
  assert.deepEqual(await manager.list(), [{ spotRid: created.spotRid }]);
  assert.equal(await manager.close(created.spotRid), true);
  assert.equal(await manager.close(created.spotRid), false);
  assert.equal(await manager.find(created.spotRid), null);
  assert.deepEqual(events, ['configure', 'onCreate:open', 'onInitialize', 'onClosing']);
});

test('ZLinkSpotManager drain applies policy per Spot type and completes from lifecycle removal', async () => {
  const closed = [];
  class NaturalSpot {
    async onClosing() { closed.push('natural'); }
  }
  class RecreatedSpot {
    async onClosing() { closed.push('recreated'); }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [NaturalSpot, RecreatedSpot],
    spotDrainPolicy: (spotType) => spotType === RecreatedSpot ? 'ReleaseAndRecreate' : 'DrainNatural'
  });
  const natural = await manager.create(NaturalSpot);
  const recreated = await manager.create(RecreatedSpot);

  let completed = false;
  const draining = manager.drainForShutdown().then(() => { completed = true; });
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(await manager.find(recreated.spotRid), null);
  assert.deepEqual(await manager.find(natural.spotRid), { spotRid: natural.spotRid });
  assert.equal(completed, false);
  assert.deepEqual(closed, ['recreated']);

  await manager.close(natural.spotRid);
  await draining;
  assert.equal(completed, true);
  assert.deepEqual(closed, ['recreated', 'natural']);
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
  await manager.getOrCreate(FailingCloseSpot, 'failing-close-room');

  const first = assert.rejects(() => manager.close('failing-close-room'), /close lifecycle failed/);
  await entered.promise;
  const second = assert.rejects(() => manager.close('failing-close-room'), /close lifecycle failed/);
  assert.equal(await manager.find('failing-close-room'), null);
  release.resolve();
  await Promise.all([first, second]);

  assert.equal(closingCalls, 1);
  assert.equal(nativeDisposes, 1);
  assert.equal(await manager.find('failing-close-room'), null);
  assert.equal(await manager.close('failing-close-room'), false);
  assert.equal(await manager.find('failing-close-room'), null);
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
    locationLifecycle: nodeA.lifecycle,
    locationMeshName: 'play'
  });
  const managerB = new framework.DefaultZLinkSpotManager({
    spotFactories: [LosingSpot],
    nodeRid: rid('node-b'),
    locationLifecycle: nodeB.lifecycle,
    locationMeshName: 'play'
  });

  const created = await managerA.getOrCreate(StageSpot, rid('room-1'));
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);

  const loser = await managerB.getOrCreate(LosingSpot, rid('room-1'));
  assert.equal(loser.state, framework.ZLinkSpotCreateState.Existing);
  assert.equal(activatedA, 1);
  assert.equal(constructedB, 0);
  assert.equal(activatedB, 0);

  await managerA.close(rid('room-1'));
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('room-1') }), undefined);
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
    locationLifecycle: node.lifecycle,
    locationMeshName: 'play'
  });

  await assert.rejects(
    () => manager.getOrCreate(FailingSpot, rid('fail-room')),
    /spot activation failed/
  );
  assert.equal(await store.resolveSpot({ meshName: 'play', spotRid: rid('fail-room') }), undefined);

  const rejected = await manager.getOrCreate(RejectingSpot, rid('reject-room'));
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
  const created = await manager.getOrCreate(StageSpot, 'stage-a');

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
  const created = await manager.getOrCreate(StageSpot, 'stage-lazy-node');

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
  const created = await manager.create(StageSpot);

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
    const created = await manager.create(CodecSpot, payload);
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

  const created = await manager.create(CodecSpot, { text: 'open' });

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

    const created = await manager.create(CodecSpot, { text: `${name}:open` });

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
      this.context.handlers.addSubscribe(SubscribeHandler, 'stage.updated');
      registry = this.context.handlers;
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  await manager.create(StageSpot);

  assert.deepEqual(registry.snapshot(), [
    { kind: 'packet', handlerType: PacketHandler, packetName: 'stage.packet' },
    { kind: 'subscribe', handlerType: SubscribeHandler, topic: 'stage.updated' }
  ]);
});

test('ZLinkSpotManager reports SPOT subscription dispatch errors to global observer', async () => {
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  let dispatchHandler;
  const subscriptionQueue = [{ topic: 'unmatched', routingId: 'source-node', parts: [] }];
  const nativeSpot = {
    routingId: 'stage-subscription',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    setSubscription() {},
    subscribe(result) {
      const next = subscriptionQueue.shift();
      if (next === undefined) {
        return false;
      }
      result.topic = next.topic;
      result.routingId = next.routingId;
      result.parts = next.parts;
      return true;
    },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: () => nativeSpot,
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  await manager.getOrCreate(StageSpot, 'stage-subscription');
  dispatchHandler({ event: 1 });
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(dispatchEvents.length, 1);
  assert.equal(dispatchEvents[0].surface, framework.ZLinkDispatchErrorSurface.SpotSubscription);
  assert.equal(dispatchEvents[0].messageKind, framework.ZLinkDispatchMessageKind.Publish);
  assert.equal(dispatchEvents[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
  assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.InvalidFrame);
  assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.Drop);
  assert.equal(dispatchEvents[0].topic, 'unmatched');
  assert.equal(dispatchEvents[0].sourceRid, 'source-node');
});

test('ZLinkSpotManager drains SPOT subscription events that arrive during dispatch', async () => {
  let dispatchHandler;
  const subscriptionQueue = [];
  const events = [];
  const nativeSpot = {
    routingId: 'stage-subscription-redrain',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    setSubscription(topic) {
      events.push(`subscribe:${topic}`);
    },
    subscribe(result) {
      const next = subscriptionQueue.shift();
      if (next === undefined) {
        return false;
      }
      result.topic = next.topic;
      result.routingId = next.routingId;
      result.parts = next.parts;
      return true;
    },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  class StageSpot {}
  class SubscribeHandler {
    async handle(_spot, event) {
      events.push(`event:${event.marker}`);
      if (event.marker === 'first') {
        subscriptionQueue.push(subscriptionMessage('stage.updated', 'second'));
        dispatchHandler({ event: 1 });
      }
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: () => nativeSpot,
    detachedTaskRunner: {
      runDetached(_taskName, callback) {
        void callback().catch((error) => events.push(`error:${error.message}`));
      }
    },
    spotSubscriptionHandlers: [{
      spotType: StageSpot,
      handlerType: SubscribeHandler,
      topic: 'stage.updated'
    }]
  });

  await manager.getOrCreate(StageSpot, 'stage-subscription-redrain');
  subscriptionQueue.push(subscriptionMessage('stage.updated', 'first'));
  dispatchHandler({ event: 1 });
  await waitFor(
    () => events.filter((event) => event.startsWith('event:')).length === 2,
    1000,
    () => `observed events: ${events.join(', ')}`
  );

  assert.deepEqual(events, [
    'subscribe:stage.updated',
    'event:first',
    'event:second'
  ]);
});

test('SPOT subscription dispatch preserves publisher flow origin', async () => {
  let dispatchHandler;
  const subscriptionQueue = [];
  const flowEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      flowEvents.push(event);
    }
  }
  const nativeSpot = {
    routingId: 'stage-subscription-flow',
    setDispatchHandler(handler) { dispatchHandler = handler; },
    setSubscription() {},
    subscribe(result) {
      const next = subscriptionQueue.shift();
      if (next === undefined) return false;
      result.topic = next.topic;
      result.routingId = next.routingId;
      result.parts = next.parts;
      return true;
    },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  class StageSpot {}
  class SubscribeHandler { async handle() {} }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: () => nativeSpot,
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} },
      framework.ZLinkMessageFlowLogMode.KeyTransitions
    ),
    detachedTaskRunner: { runDetached(_name, callback) { void callback(); } },
    spotSubscriptionHandlers: [{
      spotType: StageSpot,
      handlerType: SubscribeHandler,
      topic: 'stage.updated'
    }]
  });

  await manager.getOrCreate(StageSpot, 'stage-subscription-flow');
  subscriptionQueue.push(flowContext.runWithFlow(
    { flowId: '019f5b07-77ef-7587-8e19-6095ff11603b', flowOrigin: 'Timer' },
    () => subscriptionMessage('stage.updated', 'tick')
  ));
  dispatchHandler({ event: 1 });
  await waitFor(() => flowEvents.some((event) =>
    event.outcome === framework.ZLinkMessageFlowOutcome.Dispatched));

  assert.equal(flowEvents.every((event) => event.flowId === '019f5b07-77ef-7587-8e19-6095ff11603b'), true);
  assert.equal(flowEvents.every((event) => event.flowOrigin === 'Timer'), true);
});

test('ZLinkSpotManager reports SPOT actor dispatch errors to global observer', async () => {
  const dispatchEvents = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  let dispatchHandler;
  const badPart = zlink.Message.from('bad-frame');
  const actorParts = [{
    info: { actor: { nodeRid: 'node-a', actorId: 'actor-1', generation: 0n } },
    message: badPart,
    more: false
  }];
  const nativeSpot = {
    routingId: 'stage-actor',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    setSubscription() {},
    subscribe() { return false; },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: () => nativeSpot,
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  try {
    await manager.getOrCreate(StageSpot, 'stage-actor');
    dispatchHandler({
      event: 5,
      recvActorPart() {
        return actorParts.shift() ?? null;
      }
    });
    await new Promise((resolve) => setImmediate(resolve));

    assert.equal(dispatchEvents.length, 1);
    assert.equal(dispatchEvents[0].surface, framework.ZLinkDispatchErrorSurface.SpotActor);
    assert.equal(dispatchEvents[0].messageKind, framework.ZLinkDispatchMessageKind.ActorSend);
    assert.equal(dispatchEvents[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
    assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.InvalidFrame);
    assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.Drop);
    assert.equal(dispatchEvents[0].spotRid, 'stage-actor');
    assert.equal(dispatchEvents[0].actorId, 'actor-1');
  } finally {
    badPart.close();
  }
});

test('ZLinkSpotManager replies routed actor request dispatch errors', async () => {
  let dispatchHandler;
  const dispatchEvents = [];
  const replyMessages = [];
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const requestHeader = streamProtocol.encodeStreamHeader({
    kind: streamProtocol.ZLinkStreamMessageKind.Request,
    codec: streamProtocol.ZLinkStreamCodec.Json,
    flags: 0,
    requestSeq: 1n,
    name: 'MissingActorPacket',
    metadata: new Map()
  });
  const relay = zlink.Message.from(JSON.stringify({
    packetName: '__zlink.actor.packet.relay',
    actorId: 'actor-1',
    header: Buffer.from(requestHeader).toString('base64'),
    payload: Buffer.from('payload').toString('base64')
  }));
  const nativeSpot = {
    routingId: 'stage-routed-actor',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    setSubscription() {},
    subscribe() { return false; },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  const routed = {
    parts: [relay],
    requestSeq: 1n,
    reply() {
      return {
        message(message) {
          replyMessages.push(Buffer.from(message).toString());
          return this;
        },
        submit() {
          return true;
        }
      };
    },
    close() {}
  };
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: () => nativeSpot,
    dispatchErrors: dispatchErrorReporter(
      DispatchObserver,
      { reportRuntimeTaskException() {} }
    )
  });

  try {
    await manager.getOrCreate(StageSpot, 'stage-routed-actor');
    dispatchHandler({ event: 2, routed });
    await new Promise((resolve) => setImmediate(resolve));

    assert.equal(replyMessages.length, 1);
    assert.equal(JSON.parse(replyMessages[0]).ok, false);
    assert.equal(dispatchEvents.length, 1);
    assert.equal(dispatchEvents[0].surface, framework.ZLinkDispatchErrorSurface.SpotActor);
    assert.equal(dispatchEvents[0].outcome, framework.ZLinkMessageFlowOutcome.Error);
    assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerMissing);
    assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.ReplyError);
  } finally {
    relay.close();
  }
});

test('ZLinkSpotManager does not bind NO_BIND actor packets as remote sessions', async () => {
  let dispatchHandler;
  const noBindReplies = [];
  const boundSessions = [];
  const parts = createActorRequestParts('ActorAsk', { value: 'ping' }, 42n, 1);
  let nextPart = 0;
  const nativeSpot = {
    routingId: 'stage-no-bind',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    setSubscription() {},
    subscribe() { return false; },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  const nativeNode = {
    routingId: zlink.RoutingId.from('node-a'),
    bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid) {
      boundSessions.push({ actor, sourceNodeRid, sourceSessionRid });
    },
    replyActorNoBind(info, replyParts, result) {
      noBindReplies.push({ info, replyParts, result });
    }
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
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    createNativeSpot: () => nativeSpot,
    nativeSpotNodeProvider: () => nativeNode,
    actorResolver: () => new ProbeActor(),
    spotActorRequestHandlers: [{
      spotType: StageSpot,
      actorType: ProbeActor,
      handlerType: ProbeRequestHandler,
      packetName: 'ActorAsk'
    }]
  });

  try {
    await manager.getOrCreate(StageSpot, 'stage-no-bind');
    dispatchHandler({
      event: 5,
      recvActorPart() {
        if (nextPart !== 0) return null;
        const message = parts[nextPart++];
        return {
          info: noBindInfo(),
          message,
          more: true
        };
      }
    });
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(noBindReplies.length, 0);
    dispatchHandler({
      event: 5,
      recvActorPart() {
        if (nextPart !== 1) return null;
        const message = parts[nextPart++];
        return {
          info: noBindInfo(),
          message,
          more: false
        };
      }
    });
    await new Promise((resolve) => setImmediate(resolve));

    assert.equal(boundSessions.length, 0);
    assert.equal(noBindReplies.length, 1);
    assert.equal(noBindReplies[0].result, zlink.RequestResult.Ok);
    assert.equal(noBindReplies[0].info.requestId, 42n);
    const decoded = decodeActorReplyFrame(noBindReplies[0].replyParts[0]);
    assert.equal(decoded.header.kind, streamProtocol.ZLinkStreamMessageKind.Response);
    assert.deepEqual(decoded.payload, { value: 'ping:actor-1' });

    const backendParts = createActorSendParts('ActorAsk', { value: 'backend' });
    let backendPart = 0;
    dispatchHandler({
      event: 5,
      recvActorPart() {
        if (backendPart >= backendParts.length) return null;
        const message = backendParts[backendPart++];
        return {
          info: noBindInfo(43n, 1),
          message,
          more: backendPart < backendParts.length
        };
      }
    });
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(boundSessions.length, 0);
  } finally {
    for (const part of parts) {
      part.close();
    }
    for (const reply of noBindReplies) {
      for (const part of reply.replyParts) {
        part.close();
      }
    }
  }
});

test('ZLinkSpotManager replies no-bind actor handler exceptions as HandlerException errors', async () => {
  let dispatchHandler;
  const dispatchEvents = [];
  const noBindReplies = [];
  const boundSessions = [];
  const parts = createActorRequestParts('ThrowAsk', { value: 'boom' }, 43n, 1);
  let nextPart = 0;
  class DispatchObserver {
    onMessageFlow(event) {
      dispatchEvents.push(event);
    }
  }
  const nativeSpot = {
    routingId: 'stage-no-bind-error',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    setSubscription() {},
    subscribe() { return false; },
    recvActorLifecycle() { return null; },
    drainReply() { return 0; },
    drainChannelReply() { return 0; },
    recvRoute() { return false; },
    onSendReady() {},
    async dispose() {}
  };
  const nativeNode = {
    routingId: zlink.RoutingId.from('node-a'),
    bindRemoteActorSession(actor, sourceNodeRid, sourceSessionRid) {
      boundSessions.push({ actor, sourceNodeRid, sourceSessionRid });
    },
    replyActorNoBind(info, replyParts, result) {
      noBindReplies.push({ info, replyParts, result });
    }
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
    createNativeSpot: () => nativeSpot,
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
    await manager.getOrCreate(StageSpot, 'stage-no-bind-error');
    dispatchHandler({
      event: 5,
      recvActorPart() {
        if (nextPart >= parts.length) {
          return null;
        }
        const message = parts[nextPart];
        nextPart += 1;
        return {
          info: noBindInfo(43n),
          message,
          more: nextPart < parts.length
        };
      }
    });
    await new Promise((resolve) => setImmediate(resolve));

    assert.equal(boundSessions.length, 0);
    assert.equal(noBindReplies.length, 1);
    const decoded = decodeActorReplyFrame(noBindReplies[0].replyParts[0]);
    assert.equal(decoded.header.kind, streamProtocol.ZLinkStreamMessageKind.Error);
    assert.deepEqual(decoded.payload, { code: 'Error', message: 'handler boom', isRetriable: false });
    assert.equal(dispatchEvents.length, 1);
    assert.equal(dispatchEvents[0].errorReason, framework.ZLinkDispatchErrorReason.HandlerException);
    assert.equal(dispatchEvents[0].errorAction, framework.ZLinkDispatchErrorAction.ReplyError);
  } finally {
    for (const part of parts) {
      part.close();
    }
    for (const reply of noBindReplies) {
      for (const part of reply.replyParts) {
        part.close();
      }
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
  await manager.create(StageSpot);

  assert.deepEqual(events, ['configure', 'initialize']);
});

test('ZLinkSpotManager getOrCreate is keyed by spot type and spotRid', async () => {
  class StageSpot {}
  class OtherSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot, OtherSpot] });

  assert.deepEqual(await manager.getOrCreate(StageSpot, 'stage-1'), {
    spotRid: 'stage-1',
    state: framework.ZLinkSpotCreateState.Created,
    reply: undefined
  });
  assert.deepEqual(await manager.getOrCreate(StageSpot, 'stage-1'), {
    spotRid: 'stage-1',
    state: framework.ZLinkSpotCreateState.Existing
  });
  await assert.rejects(
    () => manager.getOrCreate(OtherSpot, 'stage-1'),
    (error) =>
      error instanceof framework.ZLinkFrameworkException &&
      error.kind === framework.ZLinkFrameworkErrorKind.SpotTypeMismatch
  );
});

test('ZLinkSpotManager list returns spot infos ordered by routing id', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });

  await manager.getOrCreate(StageSpot, 'stage-c');
  await manager.getOrCreate(StageSpot, 'stage-a');
  await manager.getOrCreate(StageSpot, 'stage-b');

  assert.deepEqual(await manager.list(), [
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

  const first = manager.getOrCreate(StageSpot, 'payload-room', 'first-a');
  await entered.promise;
  let secondSettled = false;
  const secondResult = manager.getOrCreate(StageSpot, 'payload-room', 'second').finally(() => {
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
  const first = manager.getOrCreate(StageSpot, 'same-turn-room');
  const second = manager.getOrCreate(StageSpot, 'same-turn-room');
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

  const first = manager.getOrCreate(RejectingConcurrentSpot, 'reject-room', 'first');
  await entered.promise;
  const second = manager.getOrCreate(RejectingConcurrentSpot, 'reject-room', 'second');
  release.resolve();

  const [firstResult, secondResult] = await Promise.all([first, second]);

  assert.equal(firstResult.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(secondResult.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(firstResult.reply, 'reject:first');
  assert.equal(secondResult.reply, 'reject:first');
  assert.deepEqual(payloads, ['first']);
  assert.equal(await manager.find('reject-room'), null);
});

test('ZLinkSpotManager create reject returns rejected state reply and does not register spot', async () => {
  class RejectingSpot {
    async onCreate(request) {
      return { accepted: false, reply: `reject:${request.decode()}` };
    }
  }

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [RejectingSpot] });
  const rejected = await manager.create(RejectingSpot, 'closed');

  assert.equal(rejected.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(rejected.reply, 'reject:closed');
  assert.equal(await manager.find(rejected.spotRid), null);
  assert.deepEqual(await manager.list(), []);
});

test('ZLinkSpotManager rejection disposes native Spot and releases its location exactly once', async () => {
  let nativeDisposes = 0;
  let locationReleases = 0;
  const nativeSpot = {
    routingId: 'rejected-cleanup-room',
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
    locationMeshName: 'play',
    locationLifecycle,
    createNativeSpot: () => nativeSpot
  });
  const result = await manager.getOrCreate(RejectingSpot, 'rejected-cleanup-room');

  assert.equal(result.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(nativeDisposes, 1);
  assert.equal(locationReleases, 1);
  assert.equal(await manager.close('rejected-cleanup-room'), false);
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
  const rejected = await manager.getOrCreate(RetryCreateSpot, 'retry-room', 'first');
  assert.equal(rejected.state, framework.ZLinkSpotCreateState.Rejected);
  assert.equal(rejected.reply, 'try-again');
  assert.equal(await manager.find('retry-room'), null);
  assert.deepEqual(await manager.list(), []);

  accepted = true;
  const created = await manager.getOrCreate(RetryCreateSpot, 'retry-room', 'second');
  assert.equal(created.state, framework.ZLinkSpotCreateState.Created);
  assert.deepEqual(await manager.find('retry-room'), { spotRid: 'retry-room' });
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
    () => manager.getOrCreate(FailingCreateSpot, 'create-failure-room'),
    /create failed/
  );
  assert.equal(await manager.find('create-failure-room'), null);
  const createRetry = await manager.getOrCreate(FailingCreateSpot, 'create-failure-room');
  assert.equal(createRetry.state, framework.ZLinkSpotCreateState.Created);

  await assert.rejects(
    () => manager.getOrCreate(FailingInitializeSpot, 'initialize-failure-room'),
    /initialize failed/
  );
  assert.equal(await manager.find('initialize-failure-room'), null);
  const initializeRetry = await manager.getOrCreate(FailingInitializeSpot, 'initialize-failure-room');
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
    () => manager.create(StageSpot),
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
    async onActorJoin(actorId, request) {
      events.push(`join:${actorId}:${request.decode()}`);
      return { accepted: true, reply: 'joined' };
    }
    async onJoinedActor(actor) {
      events.push(`joined:${actor.actorId}`);
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
  await manager.getOrCreate(StageSpot, 'stage-1');
  const actor = { actorId: 'alice' };
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
  await manager.getOrCreate(StageSpot, 'stage-rollback');
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
  await manager.close('stage-rollback');
  request.close();
});

test('routed actor transfer separates admission from materialization and commit', async () => {
  const events = [];
  const replies = [];
  let failJoined = false;
  let admissionGate;
  let transferInGate;
  const target = {
    async onActorJoin(actorId, request) {
      events.push(`admission:${actorId}:${request.decode()}`);
      await admissionGate;
      return { accepted: true, reply: 'admitted' };
    },
    async onJoinedActor(actor) {
      events.push(`joined:${actor.actorId}:${actor.value}`);
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
        await target.onJoinedActor(actor);
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
    'joined:alice:state-42'
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
    'joined:alice:failed-state',
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
  let dispatchHandler;
  let capturedTarget;
  const replies = [];
  const nativeJoinMessage = zlink.Message.from(JSON.stringify({
    packetName: '__zlink.actor.join_spot.request',
    actorType: 'PlayerActor',
    actorNodeRid: 'play-b',
    actorGeneration: '4',
    request: Buffer.from('join-room').toString('base64')
  }));
  let pendingNativeJoin = true;
  const nativeSpot = {
    routingId: 'room-1',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    recvActorJoin() {
      if (!pendingNativeJoin) {
        return null;
      }
      pendingNativeJoin = false;
      return {
        info: {
          sourceActor: { nodeRid: zlink.RoutingId.from('play-b'), actorId: 'player-2', generation: 4n },
          targetActor: { nodeRid: zlink.RoutingId.from('play-a'), actorId: 'player-2', generation: 5n },
          sourceNodeRid: zlink.RoutingId.from('play-b'),
          sourceSpotRid: zlink.RoutingId.from('play-b-entry'),
          targetNodeRid: zlink.RoutingId.from('play-a'),
          targetSpotRid: zlink.RoutingId.from('room-1'),
          joinEpoch: 9n,
          flags: 1
        },
        message: nativeJoinMessage
      };
    },
    replyActorJoin(_request, code) {
      return {
        message() { return this; },
        submit() {
          replies.push(code);
        }
      };
    },
    async dispose() {}
  };
  class RoomSpot {
    async onActorJoin() {
      return { accepted: true };
    }
  }
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [RoomSpot],
    createNativeSpot() {
      return nativeSpot;
    },
    routedActorProvider: async (_actorId, _actorType, _actorRef, remoteBoundSessionTarget) => {
      capturedTarget = remoteBoundSessionTarget;
      return {
        actor: { actorId: 'player-2' },
        actorRef: { nodeRid: zlink.RoutingId.from('play-a'), actorId: 'player-2', generation: 5n }
      };
    },
    nativeJoinBoundSessionTargetResolver(info) {
      return {
        routerChannelId: 'bingo.room.route',
        targetNodeRid: info.sourceActor.nodeRid,
        spotRid: info.sourceSpotRid
      };
    }
  });

  await manager.getOrCreate(RoomSpot, 'room-1');
  dispatchHandler({ event: 6 });
  await new Promise((resolve) => setImmediate(resolve));

  assert.equal(capturedTarget, undefined);
  assert.equal(replies[0], 1);
  await manager.close('room-1');
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
  const created = await manager.create(StageSpot);
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
    spotKind: framework.ZLinkSpotKind.Entry
  };
  const targetSpot = framework.createSpotHandle(
    targetSpotRef.spotRid,
    async () => targetSpotRef
  );
  const routedTransport = {
    async sendToSpot(address, message, options) {
      events.push(`send:${address.targetNodeRid}:${address.spotRid}:${address.spotKind}:${options.packetName}:${message}`);
    },
    async requestToSpot(address, request, options) {
      events.push(`request:${address.routerChannelId}:${address.spotRid}:${address.spotKind}:${options.timeoutMs}:${request}`);
      return 'routed-reply';
    }
  };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    routedTransport
  });
  const created = await manager.create(StageSpot);
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
    `send:node-b:stage-b:${framework.ZLinkSpotKind.Entry}:Notice:notice`,
    `request:play.route:stage-b:${framework.ZLinkSpotKind.Entry}:250:ping`
  ]);
});

test('spot outbound routed calls require runtime transport', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot);
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
  const created = await manager.create(StageSpot);
  const blockingTurn = manager.executeOnSpot(StageSpot, created.spotRid, async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });

  const tick = await tickReceived;
  await blockingTurn;
  await manager.close(created.spotRid);

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
  const created = await manager.create(SelfClosingSpot);
  await closed.promise;
  await waitFor(async () => (await manager.find(created.spotRid)) === null);
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
  const created = await manager.create(OccupiedSpot);

  assert.equal(await manager.close(created.spotRid), false);
  assert.deepEqual(await manager.find(created.spotRid), { spotRid: created.spotRid });
  assert.deepEqual(events, []);

  actorCount = 0;
  assert.equal(await manager.close(created.spotRid), true);
  assert.equal(await manager.find(created.spotRid), null);
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
  const created = await manager.create(OccupiedSpot);
  const blockingTurn = manager.executeOnSpot(OccupiedSpot, created.spotRid, async () => {
    turnStarted.resolve();
    await releaseTurn.promise;
  });
  await turnStarted.promise;
  const actorJoin = manager.executeOnSpot(OccupiedSpot, created.spotRid, () => {
    actorCount = 1;
  });
  const closing = manager.close(created.spotRid);

  releaseTurn.resolve();
  await blockingTurn;
  await actorJoin;

  assert.equal(await closing, false);
  assert.deepEqual(await manager.find(created.spotRid), { spotRid: created.spotRid });
});

test('spot timer rejects invalid options', async () => {
  class Handler {
    async handle() {}
  }
  class StageSpot {}

  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot);

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

function subscriptionMessage(topic, marker) {
  return {
    topic,
    routingId: 'publisher',
    parts: channelProtocol.encodeChannelPublishEnvelopeParts(
      '',
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
