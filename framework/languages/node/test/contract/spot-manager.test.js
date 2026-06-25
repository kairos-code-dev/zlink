const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');
const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const connector = require('../../packages/stream-connector/dist');
const json = connector;
const msgpack = require('../../packages/framework-codec-msgpack/dist');
const protobuf = require('../../packages/framework-codec-protobuf/dist');

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
    framework.ZLinkConfigurationException
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

test('spot manager local actor join does not wait for entry leave callback', async () => {
  const events = [];
  class StageSpot {
    async onActorJoin(actor, request) {
      events.push(`join:${actor.actorId}:${request.decode()}`);
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
        return new Promise(() => {});
      }
    }
  });
  await manager.getOrCreate(StageSpot, 'stage-1');
  const actor = { actorId: 'alice' };
  const request = zlink.Message.from('hello');
  const result = await Promise.race([
    manager.admitActorJoin('stage-1', actor, request, () => events.push('commit')),
    new Promise((_, reject) => setTimeout(() => reject(new Error('admitActorJoin timed out')), 100))
  ]);

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

test('spot manager passes native remote join source as actor bound-session target', async () => {
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

  assert.equal(capturedTarget.routerChannelId, 'bingo.room.route');
  assert.equal(String(capturedTarget.targetNodeRid), 'play-b');
  assert.equal(String(capturedTarget.spotRid), 'play-b-entry');
  assert.equal(replies[0], 0);
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

test('spot outbound routed send and request resolve remote address inside serial executor', async () => {
  const events = [];
  class StageSpot {}
  const remoteAddress = {
    routerChannelId: 'play.route',
    targetNodeRid: 'node-b',
    spotRid: 'stage-b',
    spotKind: framework.ZLinkSpotKind.User
  };
  const remoteAddressResolver = {
    async resolve(spotRid) {
      events.push(`resolve:${spotRid}`);
      return remoteAddress;
    }
  };
  const routedTransport = {
    async sendToSpot(address, message, options) {
      events.push(`send:${address.targetNodeRid}:${address.spotRid}:${options.packetName}:${message}`);
    },
    async requestToSpot(address, request, options) {
      events.push(`request:${address.routerChannelId}:${address.spotRid}:${options.timeoutMs}:${request}`);
      return 'routed-reply';
    }
  };
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    remoteAddressResolver,
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
  const send = outbound.sendToSpot('stage-b', 'notice').packetName('Notice').submit();
  const reply = await outbound.requestToSpot('stage-b', 'ping').packetName('Ping').timeout(250).submit();
  await send;
  await first;

  assert.equal(reply, 'routed-reply');
  assert.deepEqual(events, [
    'spot:start',
    'spot:end',
    'resolve:stage-b',
    'send:node-b:stage-b:Notice:notice',
    'resolve:stage-b',
    'request:play.route:stage-b:250:ping'
  ]);
});

test('spot outbound routed calls require resolver and runtime transport', async () => {
  class StageSpot {}
  const manager = new framework.DefaultZLinkSpotManager({ spotFactories: [StageSpot] });
  const created = await manager.create(StageSpot);
  let outbound;
  await manager.executeOnSpot(StageSpot, created.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  assert.throws(
    () => outbound.sendToSpot('stage-b', 'notice'),
    framework.ZLinkConfigurationException
  );

  const managerWithoutTransport = new framework.DefaultZLinkSpotManager({
    spotFactories: [StageSpot],
    remoteAddressResolver: { async resolve() { throw new Error('not used'); } }
  });
  const second = await managerWithoutTransport.create(StageSpot);
  await managerWithoutTransport.executeOnSpot(StageSpot, second.spotRid, (spot) => {
    outbound = spot.context.outbound;
  });

  assert.throws(
    () => outbound.requestToSpot('stage-b', 'ping'),
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
  assert.deepEqual(events, ['spot:start', 'spot:end', 'tick:1:spot-1', 'closing:false']);
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

async function waitFor(predicate, timeoutMs = 1000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.fail('timed out waiting for condition');
}

function dispatchErrorReporter(observerType, sink) {
  return new framework.ZLinkDispatchErrorReporter(
    undefined,
    undefined,
    sink,
    {
      diagnostics: {},
      liveMode: { mode: framework.ZLinkMessageFlowLogMode.ErrorsOnly },
      messageFlowObserverType: observerType
    }
  );
}
