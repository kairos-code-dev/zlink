const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');
const framework = require('../../packages/framework/dist/internal');
const msgpack = require('../../packages/framework-codec-msgpack/dist');
const protobuf = require('../../packages/framework-codec-protobuf/dist');

function customTextSerializer(prefix = 'custom:') {
  return {
    serialize(value) {
      return framework.ZLinkEncodedPayload.from(Buffer.from(`${prefix}${value}`));
    },
    deserialize(payload) {
      const text = Buffer.from(payload.data()).toString('utf8');
      return text.startsWith(prefix) ? text.slice(prefix.length) : text;
    }
  };
}

function encodedMessage(value) {
  return framework.ZLinkMessage.fromEncoded(zlink.Message.from(value));
}

test('ZLinkActorManager create find and getOrCreate follow dotnet actor semantics', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
    configure() {
      events.push(`configure:${this.actorId}`);
    }
  }
  class PlayerFactory {
    async create(actorId, context) {
      events.push(`create:${actorId}`);
      return new PlayerActor(actorId, context);
    }
  }

  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]])
  });

  const actor = await manager.getOrCreateActor('alice', 'player');
  assert.equal(actor.actorId, 'alice');
  assert.equal(actor.context.isJoined, false);
  assert.equal(await manager.findActor('alice'), actor);
  assert.equal(await manager.getOrCreateActor('alice', 'player'), actor);
  assert.deepEqual(events, ['create:alice', 'configure:alice']);
});

test('ZLinkActorManager create notifies Entry Spot after native actor creation', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
    configure() {
      events.push(`configure:${this.actorId}`);
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      events.push(`create:${actorId}`);
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    createActor(actorId) {
      events.push(`createNative:${actorId}`);
      return { nodeRid: 'node-a', actorId, generation: 1n };
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNode: node,
    async actorCreatedNotifier(nodeRid, actor) {
      events.push(`entryCreate:${nodeRid}:${actor.actorId}`);
    }
  });

  const actor = await manager.getOrCreateActor('alice', 'player');
  assert.equal(await manager.getOrCreateActor('alice', 'player'), actor);
  assert.deepEqual(actor.context.actorRef, { nodeRid: zlink.RoutingId.from('node-a'), actorId: 'alice', generation: 1n });

  assert.deepEqual(events, [
    'create:alice',
    'configure:alice',
    'createNative:alice',
    'entryCreate:node-a:alice'
  ]);
});

test('ZLinkActorManager resolves native actor node lazily at actor creation', async () => {
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  let node;
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNodeProvider: () => node
  });
  node = createMockSpotNode({
    createActor(actorId) {
      return { nodeRid: 'node-lazy', actorId, generation: 3n };
    }
  });

  const actor = await manager.getOrCreateActor('lazy', 'player');

  assert.deepEqual(actor.context.actorRef, { nodeRid: zlink.RoutingId.from('node-lazy'), actorId: 'lazy', generation: 3n });
});

test('ZLinkActorManager clears failed create state when Entry Spot create callback fails', async () => {
  const events = [];
  class PlayerFactory {
    create(actorId, context) {
      events.push(`create:${actorId}`);
      return { actorId, context };
    }
  }
  const node = createMockSpotNode({
    createActor(actorId) {
      events.push(`createNative:${actorId}`);
      return { nodeRid: 'node-a', actorId, generation: BigInt(events.length) };
    }
  });
  let failCreateCallback = true;
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNode: node,
    async actorCreatedNotifier(_nodeRid, actor) {
      events.push(`entryCreate:${actor.actorId}`);
      if (failCreateCallback) {
        throw new Error('entry create failed');
      }
    }
  });

  await assert.rejects(
    () => manager.create('alice', 'player'),
    /creation failed/
  );
  assert.equal(await manager.find('alice'), undefined);

  failCreateCallback = false;
  const actor = await manager.getOrCreateActor('alice', 'player');

  assert.equal(actor.actorId, 'alice');
  assert.deepEqual(events, [
    'create:alice',
    'createNative:alice',
    'entryCreate:alice',
    'create:alice',
    'createNative:alice',
    'entryCreate:alice'
  ]);
});

test('ZLinkActorManager wires actor context boundSession through runtime factory', async () => {
  const sent = [];
  const boundSession = {
    send(message) {
      return {
        metadata() { return this; },
        packetName() { return this; },
        compress() { return this; },
        async submit() {
          sent.push(message);
        }
      };
    },
    async disconnect() {
      sent.push('disconnect');
    }
  };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    boundSessionFactory(actorId) {
      assert.equal(actorId, 'alice');
      return boundSession;
    }
  });

  const actor = await manager.getOrCreateActor('alice', 'player');
  assert.equal(actor.context.boundSession, boundSession);

  await actor.context.boundSession.send({ ready: true }).submit();
  await actor.context.boundSession.disconnect();

  assert.deepEqual(sent, [{ ready: true }, 'disconnect']);
});

test('bound session disconnect does not destroy actor manager state or native actor', async () => {
  const events = [];
  const boundSession = {
    send() {
      throw new Error('not used');
    },
    async disconnect() {
      events.push('disconnectSession');
    }
  };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    createActor(actorId) {
      events.push(`createNative:${actorId}`);
      return { nodeRid: 'node-a', actorId, generation: 1n };
    },
    async destroyActor(actorRef) {
      events.push(`destroyNative:${actorRef.actorId}`);
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNode: node,
    boundSessionFactory(actorId) {
      assert.equal(actorId, 'alice');
      return boundSession;
    }
  });

  const actor = await manager.getOrCreateActor('alice', 'player');

  await actor.context.boundSession.disconnect();

  assert.equal(await manager.findActor('alice'), actor);
  assert.equal(manager.getState('alice').nativeActorRef.actorId, 'alice');
  assert.deepEqual(events, ['createNative:alice', 'disconnectSession']);
});

test('unbound actor context boundSession fails retriably until a session is bound', async () => {
  class PlayerFactory {
    create(actorId, context) {
      return { actorId, context };
    }
  }
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]])
  });
  const actor = await manager.getOrCreateActor('alice', 'player');

  assert.throws(
    () => actor.context.boundSession.send({ ready: true }),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorSessionNotBound && error.isRetriable === true
  );
  await assert.rejects(
    () => actor.context.boundSession.disconnect(),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorSessionNotBound && error.isRetriable === true
  );
});

test('ZLinkActorManager rejects duplicate create and actor type mismatch', async () => {
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  class SpectatorFactory extends PlayerFactory {}
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([
      ['player', PlayerFactory],
      ['spectator', SpectatorFactory]
    ])
  });

  await manager.create('alice', 'player');
  await assert.rejects(
    () => manager.create('alice', 'player'),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ActorAlreadyExists
  );
  await assert.rejects(
    () => manager.getOrCreate('alice', 'spectator'),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ActorTypeMismatch
  );
});

test('ZLinkActorManager shares concurrent getOrCreate actor creation', async () => {
  let releaseCreate;
  const release = new Promise((resolve) => {
    releaseCreate = resolve;
  });
  let createCount = 0;
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    async create(actorId, context) {
      createCount += 1;
      await release;
      return new PlayerActor(actorId, context);
    }
  }
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]])
  });

  const first = manager.getOrCreateActor('alice', 'player');
  const second = manager.getOrCreateActor('alice', 'player');
  releaseCreate();
  const [firstActor, secondActor] = await Promise.all([first, second]);

  assert.equal(firstActor, secondActor);
  assert.equal(createCount, 1);
});

test('ZLinkActorManager validates factory returned actor id and context', async () => {
  class WrongIdFactory {
    create(_actorId, context) {
      return { actorId: 'other', context };
    }
  }
  class WrongContextFactory {
    create(actorId) {
      return { actorId, context: {} };
    }
  }

  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([
      ['wrong-id', WrongIdFactory],
      ['wrong-context', WrongContextFactory]
    ])
  });

  for (const [actorId, actorType] of [['alice', 'wrong-id'], ['bob', 'wrong-context']]) {
    await assert.rejects(
      () => manager.create(actorId, actorType),
      (error) =>
        error instanceof framework.ZLinkFrameworkException
        && error.kind === framework.ZLinkFrameworkErrorKind.ActorCreateFailed
    );
  }
});

test('ZLinkActorDispatchMailboxSet serializes same actor and allows different actors to proceed', async () => {
  const events = [];
  const mailboxes = new framework.ZLinkActorDispatchMailboxSet();
  let releaseAlice;
  let aliceStarted;
  const aliceStartedPromise = new Promise((resolve) => {
    aliceStarted = resolve;
  });
  const releaseAlicePromise = new Promise((resolve) => {
    releaseAlice = resolve;
  });

  const aliceFirst = mailboxes.submit('alice', async () => {
    events.push('alice:first:start');
    aliceStarted();
    await releaseAlicePromise;
    events.push('alice:first:end');
  });
  await aliceStartedPromise;
  const aliceSecond = mailboxes.submit('alice', async () => {
    events.push('alice:second');
  });
  const bobFirst = mailboxes.submit('bob', async () => {
    events.push('bob:first');
  });

  await bobFirst;
  assert.deepEqual(events, ['alice:first:start', 'bob:first']);
  releaseAlice();
  await Promise.all([aliceFirst, aliceSecond]);
  assert.deepEqual(events, ['alice:first:start', 'bob:first', 'alice:first:end', 'alice:second']);
});

test('ZLinkActorDispatchRouter rechecks actor location after queued mailbox turn starts', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]])
  });
  await manager.create('alice', 'player');
  const router = new framework.ZLinkActorDispatchRouter(manager);
  let releaseFirst;
  const firstStarted = new Promise((resolve) => {
    releaseFirst = resolve;
  });
  let allowFirstToFinish;
  const firstCanFinish = new Promise((resolve) => {
    allowFirstToFinish = resolve;
  });

  const first = router.submit('alice', async (snapshot) => {
    events.push(`first:${snapshot.spotRid ?? 'entry'}`);
    releaseFirst();
    await firstCanFinish;
  });
  await firstStarted;
  const second = router.submit('alice', async (snapshot) => {
    events.push(`second:${snapshot.spotRid}`);
  });

  manager.getState('alice').setJoinedSpot('stage-1', { context: { spotRid: 'stage-1' } });
  allowFirstToFinish();
  await Promise.all([first, second]);

  assert.deepEqual(events, ['first:entry', 'second:stage-1']);
});

test('ZLinkActorContext delegates join calls to coordinator with timeout', async () => {
  const calls = [];
  const replyMessage = zlink.Message.from('joined');
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const actorRef = { nodeRid: 'node-b', actorId: 'alice', generation: 1n };
  const joinCoordinator = {
    async joinSpot(actor, state, spotRid, request, timeoutMs) {
      calls.push(`joinSpot:${actor.actorId}:${state.actorId}:${spotRid}:${request.data().toString()}:${timeoutMs}`);
      return { resultCode: 0, actor: actorRef, reply: replyMessage };
    },
    async joinEntrySpot(actor, state, nodeRid, request, timeoutMs) {
      calls.push(`joinEntry:${actor.actorId}:${state.actorId}:${nodeRid}:${request.data().toString()}:${timeoutMs}`);
      return { resultCode: 0, actor: actorRef, reply: replyMessage };
    }
  };
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator
  });
  const actor = await manager.getOrCreateActor('alice', 'player');

  const request = encodedMessage('hello');
  const joinResult = await actor.context.joinSpot('stage-1', request).timeout(25).submit();
  const entryRequest = encodedMessage('entry');
  const entryResult = await actor.context.joinEntrySpot('node-a', entryRequest).timeout(10).submit();

  assert.equal(joinResult.resultCode, 0);
  assert.deepEqual(joinResult.actor, actorRef);
  assert.equal(joinResult.reply, 'joined');
  assert.deepEqual(entryResult.actor, actorRef);
  assert.deepEqual(calls, [
    'joinSpot:alice:alice:stage-1:hello:25',
    'joinEntry:alice:alice:node-a:entry:10'
  ]);
  replyMessage.close();
});

test('ZLinkActorContext joinSpot uses configured custom serializer without raw request code', async () => {
  const calls = [];
  const replyMessage = zlink.Message.from('custom:joined');
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const actorRef = { nodeRid: 'node-b', actorId: 'alice', generation: 1n };
  const joinCoordinator = {
    async joinSpot(actor, state, spotRid, request) {
      calls.push(`joinSpot:${actor.actorId}:${state.actorId}:${spotRid}:${request.getString('utf8')}`);
      return { resultCode: 0, actor: actorRef, reply: replyMessage };
    },
    async joinEntrySpot() {
      throw new Error('joinEntrySpot must not be called');
    }
  };
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator,
    messageSerializers: new Map([['application/x-custom-text', customTextSerializer()]])
  });
  const actor = await manager.getOrCreateActor('alice', 'player');

  const joinResult = await actor.context.joinSpot('stage-1', 'hello').submit();

  assert.equal(joinResult.resultCode, 0);
  assert.equal(joinResult.reply, 'joined');
  assert.deepEqual(calls, ['joinSpot:alice:alice:stage-1:custom:hello']);
  replyMessage.close();
});

test('ZLinkActorContext joinSpot uses binary codec extensions without raw request code', async () => {
  const cases = [
    ['messagepack', msgpack.createMessagePackSerializer()],
    ['protobuf', protobuf.createProtobufMessageSerializer()]
  ];

  for (const [name, serializer] of cases) {
    const calls = [];
    const replyMessage = serializer.serialize({ text: `${name}:joined` });
    class PlayerActor {
      constructor(actorId, context) {
        this.actorId = actorId;
        this.context = context;
      }
    }
    class PlayerFactory {
      create(actorId, context) {
        return new PlayerActor(actorId, context);
      }
    }
    const actorRef = { nodeRid: 'node-b', actorId: 'alice', generation: 1n };
    const joinCoordinator = {
      async joinSpot(actor, state, spotRid, request) {
        const decoded = serializer.deserialize(request);
        calls.push(`joinSpot:${actor.actorId}:${state.actorId}:${spotRid}:${decoded.text}`);
        return { resultCode: 0, actor: actorRef, reply: replyMessage };
      },
      async joinEntrySpot() {
        throw new Error('joinEntrySpot must not be called');
      }
    };
    const manager = new framework.DefaultZLinkActorManager({
      actorFactories: new Map([['player', PlayerFactory]]),
      joinCoordinator,
      messageSerializers: new Map([[`application/x-test-${name}`, serializer]])
    });
    const actor = await manager.getOrCreateActor('alice', 'player');

    const joinResult = await actor.context.joinSpot('stage-1', { text: `${name}:hello` }).submit();

    assert.equal(joinResult.resultCode, 0);
    assert.deepEqual(joinResult.reply, { text: `${name}:joined` });
    assert.deepEqual(calls, [`joinSpot:alice:alice:stage-1:${name}:hello`]);
  }
});

test('ZLinkActorNativeJoinCoordinator creates native actor and updates joined spot state', async () => {
  const events = [];
  const createdRef = { nodeRid: 'node-a', actorId: 'alice', generation: 1n };
  const joinedRef = { nodeRid: 'node-a', actorId: 'alice', generation: 2n };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup(actorId) {
      events.push(`lookup:${actorId}`);
      return undefined;
    },
    createActor(actorId) {
      events.push(`createNative:${actorId}`);
      return createdRef;
    },
    joinActor(actorRef, targetNodeRid, targetSpotRid, payload, callback, timeoutMs) {
      events.push(`join:${actorRef.generation}:${targetNodeRid}:${targetSpotRid}:${payload.data().toString()}:${timeoutMs}`);
      callback({
        result: 0,
        joinResultCode: 7,
        actor: joinedRef,
        joinedSpotRid: targetSpotRid,
        joinEpoch: 3n,
        flags: 0
      }, [zlink.Message.from('native-reply')]);
      return true;
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({
      node
    })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');
  const request = encodedMessage('payload:hello');
  const result = await actor.context.joinSpot('stage-1', request).timeout(25).submit();

  assert.equal(result.resultCode, 7);
  assert.deepEqual(result.actor, { ...joinedRef, nodeRid: zlink.RoutingId.from('node-a') });
  assert.equal(result.reply, 'native-reply');
  assert.equal(actor.context.isJoined, true);
  assert.deepEqual(actor.context.spotRid, zlink.RoutingId.from('stage-1'));
  assert.equal(manager.getState('alice').nativeActorRef, joinedRef);
  assert.deepEqual(events, [
    'lookup:alice',
    'createNative:alice',
    'join:1:node-a:stage-1:payload:hello:25'
  ]);
});

test('ZLinkActorNativeJoinCoordinator uses native spot-node join when remote address is not a route channel', async () => {
  const events = [];
  const createdRef = { nodeRid: 'node-b', actorId: 'alice', generation: 1n };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup() {
      return undefined;
    },
    createActor() {
      return createdRef;
    },
    joinActor(actorRef, targetNodeRid, targetSpotRid, request, callback, timeoutMs) {
      const decoded = JSON.parse(request.data().toString());
      events.push(`joinActor:${actorRef.generation}:${targetNodeRid}:${targetSpotRid}:${decoded.actorType}:${Buffer.from(decoded.request, 'base64').toString()}:${timeoutMs}`);
      callback({
        result: 0,
        joinResultCode: 0,
        actor: { nodeRid: 'node-a', actorId: 'alice', generation: 2n },
        targetNodeRid,
        joinedSpotRid: targetSpotRid,
        joinEpoch: 3n,
        flags: 0
      }, [zlink.Message.from('remote-reply')]);
      return true;
    }
  });
  const remoteAddressResolver = {
    async resolve(spotRid) {
      events.push(`resolve:${spotRid}`);
      return {
        routerChannelId: 'play-node',
        targetNodeRid: 'node-a',
        spotRid,
        spotKind: framework.ZLinkSpotKind.User
      };
    }
  };
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({
      node,
      remoteAddressResolver,
      routedTransport: {
        canRoutePacketChannel(routerChannelId) {
          events.push(`canRoutePacket:${routerChannelId}`);
          return false;
        },
        canRouteChannel(routerChannelId) {
          events.push(`canRoute:${routerChannelId}`);
          return false;
        },
        async request() {
          throw new Error('route channel request must not be used for spot-node mesh joins');
        }
      },
      async remoteActorBinder(actorRef) {
        assert.equal(actorRef.nodeRid instanceof zlink.RoutingId, true);
        events.push(`bind:${actorRef.nodeRid}:${actorRef.actorId}:${actorRef.generation}`);
      }
    })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');
  const request = encodedMessage('payload');
  const result = await actor.context.joinSpot('room-1', request).submit();

  assert.equal(result.resultCode, 0);
  assert.equal(result.reply, 'remote-reply');
  assert.deepEqual(events, [
    'resolve:room-1',
    'canRoutePacket:play-node',
    'joinActor:1:node-a:room-1:player:payload:undefined',
    'bind:node-a:alice:2'
  ]);
});

test('ZLinkActorNativeJoinCoordinator routes remote spot-node join when transport owns the router channel', async () => {
  const events = [];
  const createdRef = { nodeRid: 'node-b', actorId: 'alice', generation: 1n };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup() {
      return undefined;
    },
    createActor() {
      return createdRef;
    },
    entrySpot() {
      return { routingId: 'node-b-entry' };
    },
    joinActor() {
      throw new Error('native join must not be used when the route transport can route the SPOT node channel');
    }
  });
  const remoteAddressResolver = {
    async resolve(spotRid) {
      events.push(`resolve:${spotRid}`);
      return {
        routerChannelId: 'play-node',
        targetNodeRid: 'node-a',
        spotRid,
        spotKind: framework.ZLinkSpotKind.User
      };
    }
  };
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({
      node,
      remoteAddressResolver,
      routedTransport: {
        canRoutePacketChannel(routerChannelId) {
          events.push(`canRoutePacket:${routerChannelId}`);
          return true;
        },
        canRouteChannel(routerChannelId) {
          events.push(`canRoute:${routerChannelId}`);
          return true;
        },
        async request(routerChannelId, targetNodeRid, packetName, payload) {
          events.push(`routeRequest:${routerChannelId}:${targetNodeRid}:${payload.spotRid}:${packetName}:${payload.actorId}:${payload.actorType}:${Buffer.from(payload.request, 'base64').toString()}`);
          return {
            accepted: true,
            actorNodeRid: 'node-a',
            actorId: 'alice',
            actorGeneration: '2',
            reply: Buffer.from('routed-reply').toString('base64')
          };
        }
      },
      async remoteActorBinder(actorRef) {
        events.push(`bind:${actorRef.nodeRid}:${actorRef.actorId}:${actorRef.generation}`);
      }
    })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');
  const request = encodedMessage('payload');
  const result = await actor.context.joinSpot('room-1', request).submit();

  assert.equal(result.resultCode, 0);
  assert.equal(result.reply, 'routed-reply');
  assert.deepEqual(events, [
    'resolve:room-1',
    'canRoutePacket:play-node',
    'canRoutePacket:play-node',
    'routeRequest:play-node:node-a:room-1:__zlink.actor.join_spot.request:alice:player:payload',
    'bind:node-a:alice:2'
  ]);
});

test('ZLinkActorNativeJoinCoordinator joins entry spot and clears user spot state', async () => {
  const events = [];
  const createdRef = { nodeRid: 'node-a', actorId: 'alice', generation: 1n };
  const entryRef = { nodeRid: 'node-b', actorId: 'alice', generation: 4n };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup() {
      return undefined;
    },
    createActor() {
      return createdRef;
    },
    joinActorEntrySpot(actorRef, nodeRid, request, callback, timeoutMs) {
      events.push(`joinEntry:${actorRef.generation}:${nodeRid}:${request.data().toString()}:${timeoutMs}`);
      callback({
        result: 0,
        joinResultCode: 0,
        actor: entryRef,
        targetNodeRid: nodeRid,
        joinedSpotRid: nodeRid,
        joinEpoch: 5n,
        flags: 0
      }, [zlink.Message.from('entry-ok')]);
      return true;
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({ node })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');
  manager.getState('alice').setJoinedSpot('stage-1');

  const entryRequest = encodedMessage('entry');
  const result = await actor.context.joinEntrySpot('node-b', entryRequest).timeout(50).submit();

  assert.deepEqual(result.actor, { ...entryRef, nodeRid: zlink.RoutingId.from('node-b') });
  assert.equal(actor.context.isJoined, false);
  assert.equal(actor.context.spotRid, undefined);
  assert.equal(manager.getState('alice').nativeActorRef, entryRef);
  assert.deepEqual(events, ['joinEntry:1:node-b:entry:50']);
});

test('DefaultZLinkActorManager destroys only entry-owned actors and ignores stale instances', async () => {
  const events = [];
  const createdRef = { nodeRid: 'node-a', actorId: 'alice', generation: 1n };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    createActor(actorId) {
      events.push(`createNative:${actorId}`);
      return { ...createdRef, actorId };
    },
    async destroyActor(actorRef) {
      events.push(`destroyNative:${actorRef.actorId}:${actorRef.generation}`);
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    actorDestroyedCleanup(actorId) {
      events.push(`cleanup:${actorId}`);
    }
  });

  const actor = await manager.getOrCreateActor('alice', 'player');
  manager.getState('alice').ensureNativeActorRef(node);
  manager.getState('alice').setJoinedSpot('stage-1');

  await assert.rejects(
    () => manager.destroyActor(node, zlink.RoutingId.from('node-a'), actor),
    { kind: framework.ZLinkFrameworkErrorKind.ActorRouteNotFound }
  );
  assert.equal(await manager.findActor('alice'), actor);

  manager.getState('alice').clearJoinedSpot();
  await manager.destroyActor(node, zlink.RoutingId.from('node-a'), actor);
  await manager.destroyActor(node, zlink.RoutingId.from('node-a'), actor);
  assert.equal(await manager.find('alice'), undefined);
  const router = new framework.ZLinkActorDispatchRouter(manager);
  await assert.rejects(
    () => router.submit('alice', () => 'unexpected'),
    { kind: framework.ZLinkFrameworkErrorKind.ActorRouteNotFound }
  );

  const recreated = await manager.getOrCreateActor('alice', 'player');
  manager.getState('alice').ensureNativeActorRef(node);
  await manager.destroyActor(node, zlink.RoutingId.from('node-a'), actor);
  assert.equal(await manager.findActor('alice'), recreated);

  assert.deepEqual(events, [
    'createNative:alice',
    'destroyNative:alice:1',
    'cleanup:alice',
    'createNative:alice'
  ]);
});

test('DefaultZLinkActorManager adopts native actor ref before creating routed actor instance', async () => {
  const events = [];
  const targetRef = { nodeRid: 'node-a', actorId: 'alice', generation: 9n };
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      events.push(`createApp:${actorId}`);
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup(actorId) {
      events.push(`lookupNative:${actorId}`);
      return undefined;
    },
    createActor(actorId) {
      events.push(`createNative:${actorId}`);
      throw new Error('native actor must already be owned by core join admission');
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNodeProvider: () => node
  });

  const actor = await manager.getOrCreateWithNativeRef('alice', 'player', targetRef);
  const again = await manager.getOrCreateActor('alice', 'player');

  assert.equal(again, actor);
  assert.deepEqual(manager.getState('alice').nativeActorRef, targetRef);
  assert.deepEqual(events, ['createApp:alice']);
});

test('DefaultZLinkActorManager runs destroy cleanup for local actors without native refs', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    async destroyActor(actorRef) {
      events.push(`destroyNative:${actorRef.actorId}`);
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    actorDestroyedCleanup(actorId) {
      events.push(`cleanup:${actorId}`);
    }
  });

  const actor = await manager.getOrCreateActor('local-alice', 'player');
  await manager.destroyActor(node, zlink.RoutingId.from('node-a'), actor);

  assert.equal(await manager.find('local-alice'), undefined);
  assert.deepEqual(events, ['cleanup:local-alice']);
});

test('ZLinkEntrySpotActivation destroyActor does not invoke Entry Spot lifecycle callbacks', async () => {
  const events = [];
  class EntrySpot {
    async onCreateActor(actor) {
      events.push(`entryCreate:${actor.actorId}`);
    }
    async onLeaveActor(actor) {
      events.push(`entryLeave:${actor.actorId}`);
    }
  }
  const nativeSpot = {
    routingId: 'entry-stage',
    async dispose() {}
  };
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot,
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    async destroyActor(nodeRid, actor) {
      events.push(`destroyHook:${nodeRid}:${actor.actorId}`);
    }
  });

  await activation.create();
  await activation.notifyCreateActor({ actorId: 'alice' }, framework.ZLinkMessage.from({}));
  await activation.context.destroyActor({ actorId: 'alice' });

  assert.deepEqual(events, [
    'entryCreate:alice',
    'destroyHook:node-a:alice'
  ]);
});

// ActorJoinReadable dispatch-event value (core SpotDispatchEvent.ActorJoinReadable = 6).
const ENTRY_ACTOR_JOIN_READABLE = 6;

// Drives the native recv -> admit -> reply round-trip the Entry Spot activation
// registers via setDispatchHandler. This mirrors how core delivers an admission
// request to the target node (local or remote), so the test exercises the same
// server-side admission path used in production rather than a caller-local shim.
function createEntryJoinHarness() {
  let dispatchHandler;
  const queue = [];
  const replies = [];
  let pending = 0;
  let resolveDone;
  const nativeSpot = {
    routingId: 'entry-stage',
    setDispatchHandler(handler) {
      dispatchHandler = handler;
    },
    recvActorJoin() {
      return queue.shift() ?? null;
    },
    replyActorJoin(request, code) {
      let replyMessage;
      return {
        message(message) {
          replyMessage = message;
          return this;
        },
        submit() {
          replies.push({ actorId: request.info.targetActor.actorId, code, reply: replyMessage });
          pending -= 1;
          if (pending === 0 && resolveDone !== undefined) {
            resolveDone();
          }
        }
      };
    },
    async dispose() {}
  };
  return {
    nativeSpot,
    enqueue(actorId, message) {
      queue.push({ info: { targetActor: { actorId } }, message });
      pending += 1;
    },
    async run() {
      const done = new Promise((resolve) => {
        resolveDone = pending === 0 ? resolve() : resolve;
      });
      dispatchHandler({ event: ENTRY_ACTOR_JOIN_READABLE });
      await done;
    },
    replies
  };
}

test('ZLinkEntrySpotActivation runs onActorJoin admission on the native dispatch round-trip', async () => {
  const events = [];
  class EntrySpot {
    async onActorJoin(actor, request) {
      const reason = request.decode();
      events.push(`entryJoin:${actor.actorId}:${reason}`);
      return reason === 'blocked'
        ? { accepted: false, reply: 'entry-reject-reply' }
        : { accepted: true, reply: 'entry-accept-reply' };
    }
    async onJoinedActor(actor) {
      events.push(`entryJoined:${actor.actorId}`);
    }
  }
  const harness = createEntryJoinHarness();
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: harness.nativeSpot,
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    actorResolver: (actorId) => ({ actorId }),
    async destroyActor() {}
  });
  await activation.create();
  await activation.initialize();

  const acceptRequest = zlink.Message.from('return-to-entry');
  const rejectRequest = zlink.Message.from('blocked');
  harness.enqueue('alice', acceptRequest);
  harness.enqueue('bob', rejectRequest);
  await harness.run();

  assert.equal(harness.replies[0].actorId, 'alice');
  assert.equal(harness.replies[0].code, 0);
  assert.equal(JSON.parse(harness.replies[0].reply.getString()), 'entry-accept-reply');
  assert.equal(harness.replies[1].actorId, 'bob');
  assert.equal(harness.replies[1].code, 1);
  assert.equal(JSON.parse(harness.replies[1].reply.getString()), 'entry-reject-reply');
  assert.deepEqual(events, [
    'entryJoin:alice:return-to-entry',
    'entryJoined:alice',
    'entryJoin:bob:blocked'
  ]);
  acceptRequest.close();
  rejectRequest.close();
});

test('ZLinkEntrySpotActivation auto-accepts dispatched entry join when onActorJoin is absent', async () => {
  const events = [];
  class EntrySpot {
    async onJoinedActor(actor) {
      events.push(`entryJoined:${actor.actorId}`);
    }
  }
  const harness = createEntryJoinHarness();
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: harness.nativeSpot,
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    actorResolver: (actorId) => ({ actorId }),
    async destroyActor() {}
  });
  await activation.create();
  await activation.initialize();

  const request = zlink.Message.from('return-to-entry');
  harness.enqueue('alice', request);
  await harness.run();

  assert.deepEqual(harness.replies, [{ actorId: 'alice', code: 0, reply: undefined }]);
  assert.deepEqual(events, ['entryJoined:alice']);
  request.close();
});

test('ZLinkEntrySpotActivation rejects dispatched entry join when actor is unknown', async () => {
  const events = [];
  class EntrySpot {
    async onActorJoin(actor) {
      events.push(`entryJoin:${actor.actorId}`);
      return { accepted: true };
    }
  }
  const harness = createEntryJoinHarness();
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: harness.nativeSpot,
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    actorResolver: () => undefined,
    async destroyActor() {}
  });
  await activation.create();
  await activation.initialize();

  const request = zlink.Message.from('return-to-entry');
  harness.enqueue('ghost', request);
  await harness.run();

  assert.deepEqual(harness.replies, [{ actorId: 'ghost', code: 1, reply: undefined }]);
  assert.deepEqual(events, []);
  request.close();
});

test('ZLinkActorNativeJoinCoordinator maps native join failures to framework errors', async () => {
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) {
      return new PlayerActor(actorId, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup() {
      return { nodeRid: 'node-a', actorId: 'alice', generation: 1n };
    },
    joinActor(actorRef, targetNodeRid, targetSpotRid, payload, callback) {
      callback({
        result: 109,
        joinResultCode: 0,
        actor: actorRef,
        joinedSpotRid: targetSpotRid,
        joinEpoch: 0n,
        flags: 0
      }, []);
      return true;
    }
  });
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({ node })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');

  await assert.rejects(
    () => actor.context.joinSpot('stage-1', 'hello').submit(),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ActorRouteNotFound
  );
});

test('ZLinkSpotActorDispatcher invokes send request and lifecycle handlers without fallback', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class MoveSendHandler {
    async handle(spot, actor, context, message) {
      events.push(`send:${spot.name}:${actor.actorId}:${context.packetName}:${message}`);
    }
  }
  class MoveRequestHandler {
    async handle(spot, actor, context, request) {
      events.push(`request:${spot.name}:${actor.actorId}:${context.packetName}:${request}`);
      return 'ok';
    }
  }
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime()
    .addPacket({
      kind: framework.ZLinkActorPacketKind.Send,
      packetName: 'move',
      actorType: PlayerActor,
      handlerType: MoveSendHandler
    })
    .addPacket({
      kind: framework.ZLinkActorPacketKind.Request,
      packetName: 'move',
      actorType: PlayerActor,
      handlerType: MoveRequestHandler
    });
  const actor = new PlayerActor('alice', {});
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry,
    spot: {
      name: 'game',
      async onJoinedActor(joinedActor) {
        events.push(`joined:game:${joinedActor.actorId}`);
      },
      async onLeaveActor(leftActor) {
        events.push(`left:game:${leftActor.actorId}`);
      },
      async onDisconnectActor(disconnectedActor) {
        events.push(`disconnected:game:${disconnectedActor.actorId}`);
      }
    }
  });

  await dispatcher.dispatchSend(actor, 'move', 'left');
  const reply = await dispatcher.dispatchRequest(actor, 'move', 'right');
  await dispatcher.notifyJoinActor(actor);
  await dispatcher.notifyLeaveActor(actor);
  await dispatcher.notifyDisconnectActor(actor);

  assert.equal(reply, 'ok');
  assert.deepEqual(events, [
    'send:game:alice:move:left',
    'request:game:alice:move:right',
    'joined:game:alice',
    'left:game:alice',
    'disconnected:game:alice'
  ]);
  await assert.rejects(
    () => dispatcher.dispatchRequest(actor, 'missing', 'payload'),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound
  );
});

test('ZLinkSpotActorHandlerRegistryRuntime resolves actor packets registered without actor type', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId) {
      this.actorId = actorId;
    }
  }
  class MoveRequestHandler {
    async handle(spot, actor, context, request) {
      events.push(`${spot.name}:${actor.actorId}:${context.packetName}:${request}`);
      return 'ok';
    }
  }
  const actorHandlers = new framework.ZLinkSpotActorHandlerRegistryRuntime();
  const registry = new framework.DefaultZLinkSpotHandlerRegistry(actorHandlers);
  registry.actorRequest('move', MoveRequestHandler);

  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry: actorHandlers,
    spot: { name: 'game' }
  });

  assert.equal(await dispatcher.dispatchRequest(new PlayerActor('alice'), 'move', 'x'), 'ok');
  assert.deepEqual(events, ['game:alice:move:x']);
});

test('ZLinkSpotActorDispatcher commits actor join only when onActorJoin accepts', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  const actor = new PlayerActor('alice', {});
  let accept = true;
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry: new framework.ZLinkSpotActorHandlerRegistryRuntime(),
    spot: {
      async onActorJoin(joinedActor, request) {
        events.push(`join:${joinedActor.actorId}:${request.decode()}`);
        return accept
          ? { accepted: true, reply: 'accept-reply' }
          : { accepted: false, reply: 'reject-reply' };
      },
      async onJoinedActor(joinedActor) {
        events.push(`post:${joinedActor.actorId}`);
      }
    }
  });

  const acceptedRequest = zlink.Message.from('accept');
  const accepted = await dispatcher.admitActorJoin(actor, acceptedRequest, () => {
    events.push('commit:accept');
  });
  accept = false;
  const rejectedRequest = zlink.Message.from('reject');
  const rejected = await dispatcher.admitActorJoin(actor, rejectedRequest, () => {
    events.push('commit:reject');
  });

  assert.equal(accepted.accepted, true);
  assert.equal(accepted.reply, 'accept-reply');
  assert.equal(rejected.accepted, false);
  assert.equal(rejected.reply, 'reject-reply');
  assert.deepEqual(events, [
    'join:alice:accept',
    'commit:accept',
    'post:alice',
    'join:alice:reject'
  ]);
  acceptedRequest.close();
  rejectedRequest.close();
});

test('ZLinkSpotActorDispatcher rejects actor join by default when onActorJoin is absent', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  const actor = new PlayerActor('alice', {});
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry: new framework.ZLinkSpotActorHandlerRegistryRuntime(),
    spot: {
      async onJoinedActor(joinedActor) {
        events.push(`post:${joinedActor.actorId}`);
      }
    }
  });

  const request = zlink.Message.from('join');
  const result = await dispatcher.admitActorJoin(actor, request, () => {
    events.push('commit');
  });

  assert.equal(result.accepted, false);
  assert.equal(result.reply, undefined);
  assert.deepEqual(events, []);
  request.close();
});

test('ZLinkSpotActorDispatcher does not fallback actor requests to send handlers', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class MoveSendHandler {
    async handle(_spot, actor, context, message) {
      events.push(`send:${actor.actorId}:${context.packetName}:${message}`);
    }
  }
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime()
    .addPacket({
      kind: framework.ZLinkActorPacketKind.Send,
      packetName: 'move',
      actorType: PlayerActor,
      handlerType: MoveSendHandler
    });
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry,
    spot: {}
  });
  const actor = new PlayerActor('alice', {});

  await assert.rejects(
    () => dispatcher.dispatchRequest(actor, 'move', 'right'),
    (error) =>
      error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound
  );
  assert.deepEqual(events, []);
});

test('ZLinkSpotActorDispatcher exposes dotnet actor reply metadata and compression options', async () => {
  let replyOptions;
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class MoveRequestHandler {
    async handle(_spot, _actor, context, request) {
      assert.equal(context.packetName, 'move');
      assert.equal(context.reply.metadata('reply-trace-id', `reply:${request}`), context.reply);
      assert.equal(context.reply.compress(), context.reply);
      replyOptions = context.reply;
      return { accepted: true };
    }
  }
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime()
    .addPacket({
      kind: framework.ZLinkActorPacketKind.Request,
      packetName: 'move',
      actorType: PlayerActor,
      handlerType: MoveRequestHandler
    });
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry,
    spot: {}
  });
  const actor = new PlayerActor('alice', {});

  const reply = await dispatcher.dispatchRequest(actor, 'move', 'trace-101');

  assert.deepEqual(reply, { accepted: true });
  assert.equal(replyOptions instanceof framework.DefaultZLinkSpotActorReplyOptions, true);
  const snapshot = replyOptions.snapshot();
  assert.equal(snapshot.compressPayload, true);
  assert.deepEqual([...snapshot.metadata.entries()], [['reply-trace-id', 'reply:trace-101']]);
});

test('ZLinkSpotActorDispatcher serializes user spot actor handlers on provided serial executor', async () => {
  const events = [];
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class MoveSendHandler {
    async handle(_spot, _actor, _context, message) {
      events.push(`handler:${message}`);
    }
  }
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime()
    .addPacket({
      kind: framework.ZLinkActorPacketKind.Send,
      packetName: 'move',
      actorType: PlayerActor,
      handlerType: MoveSendHandler
    });
  const serial = new framework.ZLinkSpotSerialExecutor();
  const dispatcher = new framework.ZLinkSpotActorDispatcher({
    registry,
    spot: {},
    serial
  });
  const actor = new PlayerActor('alice', {});

  const first = serial.execute(async () => {
    events.push('spot:start');
    await new Promise((resolve) => setTimeout(resolve, 5));
    events.push('spot:end');
  });
  const send = dispatcher.dispatchSend(actor, 'move', 'left');
  await Promise.all([first, send]);

  assert.deepEqual(events, ['spot:start', 'spot:end', 'handler:left']);
});

function createMockSpotNode(overrides) {
  return {
    routingId: 'node-a',
    setRoutingId() {},
    setRouterBind() {},
    setPubBind() {},
    attachDiscovery() {},
    connectPeer() {},
    disconnectPeer() {},
    createSpot() { throw new Error('not used'); },
    getOrCreateSpot() { throw new Error('not used'); },
    status() { throw new Error('not used'); },
    peers() { return []; },
    subjects() { return []; },
    entrySpot() { throw new Error('not used'); },
    createActor(actorId) {
      return { nodeRid: 'node-a', actorId, generation: 1n };
    },
    actorLookup() {
      return undefined;
    },
    joinActor() {
      throw new Error('not used');
    },
    joinActorEntrySpot() {
      throw new Error('not used');
    },
    destroyActor() {
      throw new Error('not used');
    },
    sendActorBoundSession() {
      throw new Error('not used');
    },
    closeActorBoundSession() {
      throw new Error('not used');
    },
    async dispose() {},
    nativeInstance: {},
    ...overrides
  };
}
