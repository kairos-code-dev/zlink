const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('@zlink-systems/zlink');
const framework = require('../../packages/framework/dist/internal');
const {
  ZLinkSpotNativeActorJoinAdmission
} = require('../../packages/framework/dist/runtime/spots/spot-native-actor-join-admission');
const {
  ZLinkLocalFirstActorJoinCoordinator
} = require('../../packages/framework/dist/runtime/actors/local-first-actor-join-coordinator');
const {
  ZLinkActorTransferRuntime
} = require('../../packages/framework/dist/runtime/host/actor-transfer-runtime');
const {
  ZLinkEntryActorRuntimeService
} = require('../../packages/framework/dist/runtime/host/entry-actor-runtime');
const {
  ZLinkSpotActorPacketDispatch
} = require('../../packages/framework/dist/runtime/spots/spot-actor-packet-dispatch');
const msgpack = require('../../packages/framework-codec-msgpack/dist/server/framework.cjs');
const protobuf = require('../../packages/framework-codec-protobuf/dist/server/framework.cjs');

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

test('actor transfer registry uses custom state adapters and defaults missing adapters to empty state', async () => {
  const events = [];
  class TransferActor {
    constructor(actorId, value, context) {
      this.actorId = actorId;
      this.value = value;
      this.context = context;
    }
  }
  class TransferAdapter {
    async transferOut(actor) {
      events.push(`out:${actor.actorId}:${actor.value}`);
      return framework.ZLinkMessage.from({ value: actor.value });
    }
    async transferIn(actorId, state) {
      const value = state.decode().value;
      events.push(`in:${actorId}:${value}`);
      return new TransferActor(actorId, value);
    }
  }
  const registry = new framework.ZLinkActorTransferRegistry(
    new Map([[TransferActor, TransferAdapter]])
  );
  const source = new TransferActor('alice', 41, {});
  const transferred = await registry.transferOut(source);
  assert.equal(transferred.adapterKey, 'TransferActor');
  assert.deepEqual(transferred.state.decode(), { value: 41 });
  const restored = await registry.transferIn(
    transferred.adapterKey,
    'alice',
    transferred.state
  );
  assert.equal(restored.value, 41);

  class StatelessActor {}
  const empty = await registry.transferOut(Object.assign(new StatelessActor(), {
    actorId: 'stateless',
    context: {}
  }));
  assert.equal(empty.adapterKey, undefined);
  assert.equal(empty.state.toEncodedPayload().data().length, 0);
  assert.deepEqual(events, ['out:alice:41', 'in:alice:41']);
});

test('transferred actor materialization injects context without invoking actor create lifecycle', async () => {
  const lifecycle = [];
  class TransferActor {
    constructor(actorId, value) {
      this.actorId = actorId;
      this.value = value;
    }
  }
  class TransferAdapter {
    async transferOut(actor) {
      return framework.ZLinkMessage.from({ value: actor.value });
    }
    async transferIn(actorId, state) {
      return new TransferActor(actorId, state.decode().value);
    }
  }
  class TransferFactory {
    create(actorId, context) {
      lifecycle.push('factory');
      return new TransferActor(actorId, 0, context);
    }
  }
  const node = createMockSpotNode({
    actorLookup() { return undefined; },
    createActor(actorId) {
      return { nodeRid: 'target-node', actorId, generation: 2n };
    },
    async destroyActor(actorRef) {
      lifecycle.push(`destroy:${actorRef.actorId}:${actorRef.generation}`);
    }
  });
  const registry = new framework.ZLinkActorTransferRegistry(
    new Map([[TransferActor, TransferAdapter]])
  );
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['transfer', TransferFactory]]),
    nativeActorNode: node,
    actorTransferRegistry: registry,
    async actorCreatedNotifier() {
      lifecycle.push('onCreateActor');
    },
    actorDestroyedCleanup(actorId) {
      lifecycle.push(`cleanup:${actorId}`);
    }
  });
  const result = await manager.materializeTransferredActor(
    'alice',
    'transfer',
    'TransferActor',
    framework.ZLinkMessage.from({ value: 77 })
  );
  assert.equal(result.actor.value, 77);
  assert.equal(result.actor.context, manager.getState('alice').actor.context);
  assert.equal(String(result.actorRef.nodeRid), 'target-node');
  assert.deepEqual(lifecycle, []);

  manager.getState('alice').setRemoteBoundSessionTarget({
    routerChannelId: 'session-route',
    targetNodeRid: 'session-a',
    spotRid: 'session-entry'
  });
  await manager.rollbackTransferredActor(result.actor);
  assert.equal(manager.getState('alice'), undefined);
  assert.deepEqual(lifecycle, ['destroy:alice:2', 'cleanup:alice']);
});

test('transferred actor rollback keeps a dispatch-disabled tombstone until native destroy retry succeeds', async () => {
  class TransferActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class TransferFactory {
    create(actorId, context) {
      return new TransferActor(actorId, context);
    }
  }
  let destroyAttempts = 0;
  let cleanupCount = 0;
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['transfer', TransferFactory]]),
    nativeActorNode: createMockSpotNode({
      createActor(actorId) {
        return { nodeRid: 'target-node', actorId, generation: 2n };
      },
      async destroyActor() {
        destroyAttempts += 1;
        if (destroyAttempts === 1) throw new Error('temporary native destroy failure');
      }
    }),
    actorDestroyedCleanup() {
      cleanupCount += 1;
    }
  });
  const { actor } = await manager.materializeTransferredActor(
    'rollback-retry',
    'transfer',
    undefined,
    framework.ZLinkMessage.fromEncoded(framework.ZLinkEncodedPayload.from(Buffer.alloc(0)))
  );

  await assert.rejects(() => manager.rollbackTransferredActor(actor), /temporary native destroy failure/);
  assert.equal(manager.getState('rollback-retry').isMoving, true);
  assert.equal(cleanupCount, 0);
  await new Promise((resolve) => setTimeout(resolve, 60));
  assert.equal(destroyAttempts, 2);
  assert.equal(manager.getState('rollback-retry'), undefined);
  assert.equal(cleanupCount, 1);
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
  assert.deepEqual(actor.context.actorRef, { nodeRid: 'node-a', actorId: 'alice', generation: 1n });

  assert.deepEqual(events, [
    'create:alice',
    'configure:alice',
    'createNative:alice',
    'entryCreate:node-a:alice'
  ]);
});

test('ZLinkActorManager claims location before activation and releases on destroy', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const nodeA = await locationLifecycleNode(store, 'owner-a', 'node-a');
  const nodeB = await locationLifecycleNode(store, 'owner-b', 'node-b');
  let activatedA = 0;
  let activatedB = 0;

  class PlayerFactory {
    async create(actorId, context) {
      activatedA++;
      const row = await store.resolveActor({ actorType: 'player', actorId });
      assert.equal(row.ownerId, 'owner-a');
      return { actorId, context };
    }
  }
  class LosingFactory {
    create(actorId, context) {
      activatedB++;
      return { actorId, context };
    }
  }

  const nativeNodeA = createMockSpotNode({
    routingId: rid('node-a'),
    createActor(actorId) {
      return { nodeRid: rid('node-a'), actorId, generation: 7n };
    },
    async destroyActor() {}
  });
  const nativeNodeB = createMockSpotNode({
    routingId: rid('node-b'),
    createActor(actorId) {
      return { nodeRid: rid('node-b'), actorId, generation: 1n };
    }
  });
  const managerA = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNode: nativeNodeA,
    locationLifecycle: nodeA.lifecycle
  });
  const managerB = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', LosingFactory]]),
    nativeActorNode: nativeNodeB,
    locationLifecycle: nodeB.lifecycle
  });

  await managerA.create('alice', 'player');
  const row = await store.resolveActor({ actorType: 'player', actorId: 'alice' });
  assert.equal(row.ownerId, 'owner-a');
  assert.equal(row.nodeRid, 'node-a');
  assert.deepEqual(row.actorRef, { nodeRid: 'node-a', actorId: 'alice', generation: 7n });

  await assert.rejects(
    () => managerB.create('alice', 'player'),
    /location claim conflict/
  );
  assert.equal(activatedA, 1);
  assert.equal(activatedB, 0);

  await managerA.destroyActor(nativeNodeA, rid('node-a'), managerA.getState('alice').actor);
  assert.equal(await store.resolveActor({ actorType: 'player', actorId: 'alice' }), undefined);
});

test('remote actor takeover fences a stale source release by location generation', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const source = await locationLifecycleNode(store, 'owner-source', 'node-source');
  const target = await locationLifecycleNode(store, 'owner-target', 'node-target');

  const sourceClaim = await source.lifecycle.claimActor('player', 'alice', rid('node-source'));
  assert.equal(sourceClaim.status, framework.ZLinkActorClaimStatus.Claimed);
  await source.lifecycle.setActorRef('player', 'alice', {
    nodeRid: rid('node-source'),
    actorId: 'alice',
    generation: 4n
  });
  const sourceRow = await store.resolveActor({ actorType: 'player', actorId: 'alice' });

  const takeover = await target.lifecycle.takeoverActorJoinedSpot(
    'player',
    'alice',
    { nodeRid: rid('node-target'), actorId: 'alice', generation: 5n },
    'play',
    rid('room-target')
  );
  assert.equal(takeover.status, framework.ZLinkActorClaimStatus.Claimed);
  const targetRow = await store.resolveActor({ actorType: 'player', actorId: 'alice' });
  assert.equal(targetRow.ownerId, 'owner-target');
  assert.equal(targetRow.locationKind, framework.ZLinkSpotKind.User);
  assert.equal(String(targetRow.spotRid), 'room-target');
  assert.equal(targetRow.generation > sourceRow.generation, true);

  await source.lifecycle.releaseActor('player', 'alice');
  const afterStaleRelease = await store.resolveActor({ actorType: 'player', actorId: 'alice' });
  assert.equal(afterStaleRelease.ownerId, 'owner-target');
  assert.equal(afterStaleRelease.generation, targetRow.generation);
  assert.equal(String(afterStaleRelease.actorRef.nodeRid), 'node-target');
});

test('remote actor takeover preserves moving source state until the commit reply installs the target ref', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const source = await locationLifecycleNode(store, 'owner-source', 'node-source');
  const target = await locationLifecycleNode(store, 'owner-target', 'node-target');
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
  let destroyedCleanup = 0;
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNode: createMockSpotNode({
      routingId: rid('node-source'),
      createActor(actorId) {
        return { nodeRid: rid('node-source'), actorId, generation: 1n };
      }
    }),
    locationLifecycle: source.lifecycle,
    actorDestroyedCleanup() {
      destroyedCleanup += 1;
    }
  });
  const actor = await manager.getOrCreateActor('alice', 'player');
  const state = manager.getState('alice');
  state.beginMove();

  const takeover = await target.lifecycle.takeoverActorJoinedSpot(
    'player',
    'alice',
    { nodeRid: rid('node-target'), actorId: 'alice', generation: 2n },
    'play',
    rid('room-target')
  );
  await Promise.resolve();

  assert.equal(takeover.status, framework.ZLinkActorClaimStatus.Claimed);
  assert.equal(manager.getState('alice').actor, actor);
  assert.equal(destroyedCleanup, 0);
});

test('ZLinkActorManager rolls location claim back when activation fails', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const node = await locationLifecycleNode(store, 'owner-a', 'node-a');

  class FailingFactory {
    create() {
      throw new Error('actor factory failed');
    }
  }

  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', FailingFactory]]),
    nativeActorNode: createMockSpotNode({ routingId: rid('node-a') }),
    locationLifecycle: node.lifecycle
  });

  await assert.rejects(
    () => manager.create('alice', 'player'),
    /actor factory failed/
  );
  assert.equal(await store.resolveActor({ actorType: 'player', actorId: 'alice' }), undefined);
  assert.equal(await manager.find('alice'), undefined);
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

  assert.deepEqual(actor.context.actorRef, { nodeRid: 'node-lazy', actorId: 'lazy', generation: 3n });
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

  actor.context.boundSession.send({ ready: true }).submit();
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
      return { accepted: true, actor: actorRef, reply: replyMessage };
    },
    async joinEntrySpot(actor, state, nodeRid, request, timeoutMs) {
      calls.push(`joinEntry:${actor.actorId}:${state.actorId}:${nodeRid}:${request.data().toString()}:${timeoutMs}`);
      return { accepted: true, actor: actorRef, reply: replyMessage };
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
  const emptyEntryResult = await actor.context.joinEntrySpot('node-b', undefined).timeout(5).submit();

  assert.equal(joinResult.status, 'accepted');
  assert.deepEqual(joinResult.actor, actorRef);
  assert.equal(joinResult.reply, 'joined');
  assert.deepEqual(entryResult.actor, actorRef);
  assert.deepEqual(emptyEntryResult.actor, actorRef);
  assert.deepEqual(calls, [
    'joinSpot:alice:alice:stage-1:hello:25',
    'joinEntry:alice:alice:node-a:entry:10',
    'joinEntry:alice:alice:node-b::5'
  ]);
  replyMessage.close();
});

test('local actor join publishes committed location only after joined callback completes', async () => {
  const events = [];
  const actor = { actorId: 'alice' };
  const state = {
    actorType: 'player',
    nativeActorRef: { nodeRid: rid('node-a'), actorId: 'alice', generation: 1n },
    setJoinedSpot() { events.push('membership'); }
  };
  const coordinator = new ZLinkLocalFirstActorJoinCoordinator({
    localSpotManager: () => ({
      hasActiveSpot: () => true,
      async admitActorJoin(_spotRid, _actor, _request, commit) {
        events.push('admission');
        commit({});
        events.push('joined');
        return { accepted: true };
      }
    }),
    nativeNode: () => ({ routingId: rid('node-a') }),
    native: { joinSpot() { throw new Error('native join must not run'); } },
    locationLifecycle: () => ({
      async notifyActorJoinedSpot(actorType, actorId, meshName, spotRid) {
        events.push(`location:${actorType}:${actorId}:${meshName}:${spotRid}`);
      }
    }),
    localSpotMeshName: () => 'play',
    async actorBinder() { events.push('bind'); }
  });

  const result = await coordinator.joinSpot(actor, state, 'room-1', zlink.Message.from('join'));

  assert.equal(result.accepted, true);
  assert.deepEqual(events, [
    'admission',
    'membership',
    'joined',
    'location:player:alice:play:room-1',
    'bind'
  ]);
});

test('post-commit actor binding failure is retried without changing an accepted join to failure', async () => {
  let bindAttempts = 0;
  const reported = [];
  const coordinator = new ZLinkLocalFirstActorJoinCoordinator({
    localSpotManager: () => ({
      hasActiveSpot: () => true,
      async admitActorJoin(_spotRid, _actor, _request, commit) {
        commit({});
        return { accepted: true };
      }
    }),
    nativeNode: () => ({ routingId: rid('node-a') }),
    native: { joinSpot() { throw new Error('native join must not run'); } },
    async actorBinder() {
      bindAttempts += 1;
      if (bindAttempts === 1) throw new Error('binding route is still connecting');
    },
    postCommitErrorReporter(error) {
      reported.push(error.message);
    }
  });
  const actor = { actorId: 'alice' };
  const state = {
    nativeActorRef: { nodeRid: rid('node-a'), actorId: 'alice', generation: 1n },
    setJoinedSpot() {}
  };

  const result = await coordinator.joinSpot(actor, state, 'room-1', zlink.Message.from('join'));

  assert.equal(result.accepted, true);
  assert.equal(bindAttempts, 1);
  await new Promise((resolve) => setTimeout(resolve, 40));
  assert.equal(bindAttempts, 2);
  assert.deepEqual(reported, ['binding route is still connecting']);
});

test('actor directory find/ensure uses id lookup and exposes actor ref snapshots', async () => {
  class PlayerFactory {
    create(actorId, context) {
      return { actorId, context };
    }
  }
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    actorCreatedNodeRidProvider: () => rid('node-a')
  });

  assert.equal(await manager.find('alice'), undefined);
  const ensured = await manager.ensure('alice', { actorType: 'player', displayName: 'Alice' });
  const found = await manager.find('alice');
  const existing = await manager.ensure('alice', { actorType: 'player', displayName: 'Ignored' });
  const snapshot = framework.zlinkActorRefSnapshotFrom(ensured);

  assert.deepEqual(found, ensured);
  assert.deepEqual(existing, ensured);
  assert.deepEqual(framework.zlinkActorRefSnapshotToActorRef(snapshot), ensured);
  assert.equal(framework.zlinkActorRefSnapshotToActorRef({ ...snapshot, generation: '42' }).generation, 42n);
});

test('actor directory ensure rejects create failures with ActorCreateRejected', async () => {
  class FailingFactory {
    create() {
      throw new Error('admission denied');
    }
  }
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', FailingFactory]])
  });

  await assert.rejects(
    () => manager.ensure('alice', { actorType: 'player' }),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.ActorCreateRejected
  );
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
      return { accepted: true, actor: actorRef, reply: replyMessage };
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

  assert.equal(joinResult.status, 'accepted');
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
        return { accepted: true, actor: actorRef, reply: replyMessage };
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

    assert.equal(joinResult.status, 'accepted');
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

  assert.equal(result.status, 'accepted');
  assert.deepEqual(result.actor, { ...joinedRef, nodeRid: 'node-a' });
  assert.equal(result.reply, 'native-reply');
  assert.equal(actor.context.spotRid, 'stage-1');
  assert.equal(manager.getState('alice').nativeActorRef, joinedRef);
  assert.deepEqual(events, [
    'lookup:alice',
    'createNative:alice',
    'join:1:node-a:stage-1:payload:hello:25'
  ]);
});

test('ZLinkActorNativeJoinCoordinator rejects remote joins without the two-phase routed transport', async () => {
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
  const spotRouteResolver = {
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
      spotRouteResolver,
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
  await assert.rejects(
    () => actor.context.joinSpot('room-1', request).submit(),
    /requires the two-phase routed transfer protocol/
  );
  assert.deepEqual(events, [
    'resolve:room-1',
    'canRoutePacket:play-node'
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
  const spotRouteResolver = {
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
      spotRouteResolver,
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
          events.push(`routeRequest:${payload.phase}:${routerChannelId}:${targetNodeRid}:${payload.spotRid}:${packetName}:${payload.actorId}:${payload.actorType}:${Buffer.from(payload.request, 'base64').toString()}`);
          if (payload.phase === 'admission') {
            assert.equal(typeof payload.transferId, 'string');
            return {
              accepted: true,
              actorNodeRid: 'node-b',
              actorId: 'alice',
              actorGeneration: '1',
              reply: Buffer.from('routed-reply').toString('base64')
            };
          }
          assert.equal(payload.phase, 'commit');
          assert.equal(payload.transferState, '');
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
      },
      sourceTransfer: {
        async prepareSource(joinedActor) {
          return {
            state: framework.ZLinkMessage.fromEncoded(
              framework.ZLinkEncodedPayload.from(Buffer.alloc(0))
            ),
            handoffBacklog: [],
            commit() {
              events.push(`leave:${joinedActor.actorId}`);
            },
            async rollback() {}
          };
        }
      }
    })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');
  const request = encodedMessage('payload');
  const result = await actor.context.joinSpot('room-1', request).submit();

  assert.equal(result.status, 'accepted');
  assert.equal(result.reply, 'routed-reply');
  assert.deepEqual(events, [
    'resolve:room-1',
    'canRoutePacket:play-node',
    'canRoutePacket:play-node',
    'routeRequest:admission:play-node:node-a:room-1:__zlink.actor.join_spot.request:alice:player:payload',
    'canRoutePacket:play-node',
    'routeRequest:commit:play-node:node-a:room-1:__zlink.actor.join_spot.request:alice:player:payload',
    'leave:alice',
    'bind:node-a:alice:2'
  ]);
});

test('remote transfer failures before commit preserve source ownership and never bind the target', async () => {
  async function runFailure(failurePoint) {
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
    class PlayerTransferAdapter {
      async transferOut() {
        events.push('transferOut');
        if (failurePoint === 'transferOut') {
          throw new Error('injected transferOut failure');
        }
        return framework.ZLinkMessage.from({ version: 1 });
      }
      async transferIn() {
        throw new Error('target transferIn is not part of the source contract test');
      }
    }
    const node = createMockSpotNode({
      routingId: rid('node-source'),
      entrySpot() {
        return { routingId: rid('entry-source') };
      },
      createActor(actorId) {
        return { nodeRid: rid('node-source'), actorId, generation: 1n };
      }
    });
    const transferRegistry = new framework.ZLinkActorTransferRegistry(
      new Map([[PlayerActor, PlayerTransferAdapter]])
    );
    const manager = new framework.DefaultZLinkActorManager({
      actorFactories: new Map([['player', PlayerFactory]]),
      joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({
        node,
        spotRouteResolver: {
          async resolve(spotRid) {
            return {
              routerChannelId: 'play',
              targetNodeRid: rid('node-target'),
              spotRid,
              spotKind: framework.ZLinkSpotKind.User
            };
          }
        },
        routedTransport: {
          canRoutePacketChannel() { return true; },
          async request(_channel, _nodeRid, _packetName, payload) {
            events.push(`request:${payload.phase}`);
            assert.equal(payload.phase, 'admission');
            return {
              accepted: true,
              actorNodeRid: 'node-source',
              actorId: 'alice',
              actorGeneration: '1'
            };
          }
        },
        sourceTransfer: {
          async prepareSource(joinedActor, state, signal) {
            events.push('move:start');
            state.beginMove();
            try {
              const transfer = await transferRegistry.transferOut(joinedActor, signal);
              events.push('prepare');
              if (failurePoint === 'prepare') {
                throw new Error('injected prepare failure');
              }
              return {
                ...transfer,
                handoffBacklog: [],
                commit() {},
                async rollback() {
                  events.push('move:cancel');
                  state.endMove();
                }
              };
            } catch (error) {
              events.push('move:cancel');
              state.endMove();
              throw error;
            }
          }
        },
        async remoteActorBinder() {
          events.push('bind');
        }
      })
    });
    const actor = await manager.getOrCreateActor('alice', 'player');
    await assert.rejects(
      () => actor.context.joinSpot('room-target', encodedMessage('join')).submit(),
      new RegExp(`injected ${failurePoint} failure`)
    );
    assert.equal(manager.getState('alice').isMoving, false);
    assert.equal(manager.getState('alice').nativeActorRef.nodeRid.toHex(), rid('node-source').toHex());
    assert.equal(events.includes('bind'), false);
    assert.equal(events.some((event) => event === 'request:commit'), false);
    return events;
  }

  assert.deepEqual(await runFailure('transferOut'), [
    'request:admission',
    'move:start',
    'transferOut',
    'move:cancel'
  ]);
  assert.deepEqual(await runFailure('prepare'), [
    'request:admission',
    'move:start',
    'transferOut',
    'prepare',
    'move:cancel'
  ]);
});

test('remote transfer commit rejection rolls prepared source movement back', async () => {
  class PlayerActor {
    constructor(actorId, context) {
      this.actorId = actorId;
      this.context = context;
    }
  }
  class PlayerFactory {
    create(actorId, context) { return new PlayerActor(actorId, context); }
  }
  const node = createMockSpotNode({
    routingId: rid('node-source'),
    entrySpot() { return { routingId: rid('entry-source') }; },
    createActor(actorId) { return { nodeRid: rid('node-source'), actorId, generation: 1n }; }
  });
  let rolledBack = false;
  let committed = false;
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    joinCoordinator: new framework.ZLinkActorNativeJoinCoordinator({
      node,
      spotRouteResolver: {
        async resolve(spotRid) {
          return {
            routerChannelId: 'play',
            targetNodeRid: rid('node-target'),
            spotRid,
            spotKind: framework.ZLinkSpotKind.User
          };
        }
      },
      routedTransport: {
        canRoutePacketChannel() { return true; },
        async request(_channel, _nodeRid, _packetName, payload) {
          return payload.phase === 'admission'
            ? {
                accepted: true,
                actorNodeRid: 'node-source',
                actorId: 'alice',
                actorGeneration: '1'
              }
            : {
                accepted: false,
                actorNodeRid: 'node-source',
                actorId: 'alice',
                actorGeneration: '1'
              };
        }
      },
      sourceTransfer: {
        async prepareSource(_actor, state) {
          state.beginMove();
          return {
            state: framework.ZLinkMessage.from({ version: 1 }),
            handoffBacklog: [],
            commit() { committed = true; },
            async rollback() {
              rolledBack = true;
              state.endMove();
            }
          };
        }
      }
    })
  });
  const actor = await manager.getOrCreateActor('alice', 'player');

  const result = await actor.context.joinSpot('room-target', encodedMessage('join')).submit();

  assert.equal(result.status, 'rejected');
  assert.equal(rolledBack, true);
  assert.equal(committed, false);
  assert.equal(manager.getState('alice').isMoving, false);
  assert.equal(manager.getState('alice').nativeActorRef.nodeRid.toHex(), rid('node-source').toHex());
});

test('target ownership publication failure releases the claimed actor location', async () => {
  const actor = { actorId: 'alice' };
  let released = 0;
  let ownsLocation = false;
  let locationGeneration;
  const state = {
    actorType: 'player',
    nativeActorRef: { nodeRid: rid('target-node'), actorId: 'alice', generation: 9n },
    remoteBoundSessionTarget: {
      routerChannelId: 'session.route',
      targetNodeRid: rid('source-node'),
      spotRid: rid('source-entry')
    },
    clearAfterDestroy() {},
    get locationGeneration() { return locationGeneration; },
    setLocationGeneration(value) { locationGeneration = value; },
    get ownsLocation() { return ownsLocation; },
    markLocationOwned() { ownsLocation = true; },
    markLocationReleased() { ownsLocation = false; }
  };
  const lifecycle = {
    async takeoverActorJoinedSpot() {
      return { status: 'claimed', claimed: { generation: 17n } };
    },
    async releaseActor() { released++; },
    async releaseActorEventually() { throw new Error('not used'); }
  };
  const runtime = new ZLinkActorTransferRuntime({
    routeTransport: {
      async sendToSpot() { throw new Error('ownership route unavailable'); }
    },
    spotManager: () => undefined,
    actorManager: () => ({
      getState: () => state,
      async rollbackTransferredActor() {}
    }),
    primarySpotNode: () => { throw new Error('not used'); },
    async notifyEntrySpotActorLeft() {},
    locationLifecycle: () => lifecycle,
    actorHandoff: {},
    actorTransferRegistry: {},
    clearRemoteActorPacketTarget() {}
  });

  await assert.rejects(
    async () => {
      try {
        await runtime.claimRoutedActorLocation(actor, rid('room'), 'play');
        await runtime.publishRoutedActorOwnership(actor);
      } catch (error) {
        await runtime.rollbackRoutedActor(actor);
        throw error;
      }
    },
    /bound-session ownership update failed/
  );
  assert.equal(released, 1);
  assert.equal(ownsLocation, false);
  assert.equal(locationGeneration, 17n);
});

test('native actor join rollback restores Entry location without destroying the existing actor', async () => {
  const actor = { actorId: 'alice' };
  let restoredToEntry = 0;
  let destroyed = 0;
  let joined = true;
  let ownsLocation = true;
  const state = {
    actorType: 'player',
    get ownsLocation() { return ownsLocation; },
    clearJoinedSpot() { joined = false; },
    markLocationReleased() { ownsLocation = false; }
  };
  const runtime = new ZLinkActorTransferRuntime({
    routeTransport: {},
    spotManager: () => undefined,
    actorManager: () => ({
      getState: () => state,
      async rollbackTransferredActor() { destroyed++; }
    }),
    primarySpotNode: () => { throw new Error('not used'); },
    async notifyEntrySpotActorLeft() {},
    locationLifecycle: () => ({
      async notifyActorLeftSpot() { restoredToEntry++; }
    }),
    actorHandoff: {},
    actorTransferRegistry: {},
    clearRemoteActorPacketTarget() {}
  });

  await runtime.rollbackNativeActorJoin(actor, {});
  assert.equal(joined, false);
  assert.equal(restoredToEntry, 1);
  assert.equal(ownsLocation, true);
  assert.equal(destroyed, 0);
});

test('native actor join rollback restores the previous User SPOT location', async () => {
  const actor = { actorId: 'alice' };
  const previousSpot = { name: 'source-room' };
  const previousSpotRid = rid('source-room');
  let currentSpotRid = rid('target-room');
  let currentSpot = { name: 'target-room' };
  let restoredLocationRid;
  let generation = 7n;
  const state = {
    actorType: 'player',
    nativeActorRef: { nodeRid: rid('entry-node'), actorId: 'alice', generation: 4n },
    ownsLocation: true,
    setJoinedSpot(spotRid, spot) { currentSpotRid = spotRid; currentSpot = spot; },
    setLocationGeneration(value) { generation = value; },
    clearAfterDestroy() {}
  };
  const runtime = new ZLinkActorTransferRuntime({
    routeTransport: {},
    spotManager: () => undefined,
    actorManager: () => ({ getState: () => state }),
    primarySpotNode: () => { throw new Error('not used'); },
    async notifyEntrySpotActorLeft() {},
    locationLifecycle: () => ({
      async takeoverActorJoinedSpot(_type, _id, _ref, meshName, spotRid) {
        assert.equal(meshName, 'source-mesh');
        restoredLocationRid = spotRid;
        return { status: 'claimed', claimed: { generation: 8n } };
      }
    }),
    actorHandoff: {},
    actorTransferRegistry: {},
    clearRemoteActorPacketTarget() {}
  });

  await runtime.rollbackNativeActorJoin(
    actor,
    { spotRid: previousSpotRid, spot: previousSpot, spotMeshName: 'source-mesh' }
  );
  assert.equal(currentSpotRid.toHex(), previousSpotRid.toHex());
  assert.equal(currentSpot, previousSpot);
  assert.equal(restoredLocationRid.toHex(), previousSpotRid.toHex());
  assert.equal(generation, 8n);
});

test('Entry actor transaction keeps committed entry state when joined callback rejects', async () => {
  const actor = { actorId: 'alice' };
  const previousSpot = { name: 'room' };
  const previousRef = { nodeRid: rid('old-node'), actorId: 'alice', generation: 4n };
  let spotRid = rid('room-node');
  let spot = previousSpot;
  let nativeActorRef = previousRef;
  let clearedTargets = 0;
  const state = {
    actor,
    get spotRid() { return spotRid; },
    get spot() { return spot; },
    get nativeActorRef() { return nativeActorRef; },
    clearJoinedSpot() { spotRid = undefined; spot = undefined; },
    setJoinedSpot(value, target) { spotRid = value; spot = target; },
    setNativeActorRef(value) { nativeActorRef = value; }
  };
  const runtime = new ZLinkEntryActorRuntimeService({
    actorManager: () => ({ getState: () => state }),
    spotManager: () => undefined,
    spotNodeRuntime: () => ({ primaryNode: { routingId: rid('entry-node') } }),
    streamBindingRuntime: { async refreshActor() {} },
    boundSessionRelay: { clearRemoteActorPacketTarget() { clearedTargets++; } }
  });

  await assert.rejects(
    () => runtime.commitActorTransaction(actor, async () => { throw new Error('joined failed'); }),
    /joined failed/
  );
  assert.equal(spotRid, undefined);
  assert.equal(spot, undefined);
  assert.equal(nativeActorRef.nodeRid.toHex(), rid('entry-node').toHex());
  assert.equal(clearedTargets, 1);
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

  assert.deepEqual(result.actor, { ...entryRef, nodeRid: 'node-b' });
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
  assert.equal(await manager.find('alice'), undefined);

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

test('DefaultZLinkActorManager adopts remote native actor ref without claiming actor location', async () => {
  const store = new framework.ZLinkInMemoryLocationStore(() => new Date(Date.UTC(2026, 6, 3, 0, 0, 0)));
  const owner = await locationLifecycleNode(store, 'owner-a', 'node-a');
  const remote = await locationLifecycleNode(store, 'owner-b', 'node-b');

  class PlayerFactory {
    create(actorId, context) {
      return { actorId, context };
    }
  }

  const ownerNode = createMockSpotNode({
    routingId: rid('node-a'),
    createActor(actorId) {
      return { nodeRid: rid('node-a'), actorId, generation: 1n };
    }
  });
  const ownerManager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNode: ownerNode,
    locationLifecycle: owner.lifecycle
  });

  await ownerManager.create('alice', 'player');
  const ownedRow = await store.resolveActor({ actorType: 'player', actorId: 'alice' });
  assert.equal(ownedRow.ownerId, 'owner-a');

  const remoteManager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', PlayerFactory]]),
    nativeActorNodeProvider: () => createMockSpotNode({ routingId: rid('node-b') }),
    locationLifecycle: remote.lifecycle
  });
  const actor = await remoteManager.getOrCreateWithNativeRef(
    'alice',
    'player',
    { nodeRid: rid('node-a'), actorId: 'alice', generation: 1n }
  );

  assert.equal(actor.actorId, 'alice');
  const rowAfterRemoteAdoption = await store.resolveActor({ actorType: 'player', actorId: 'alice' });
  assert.equal(rowAfterRemoteAdoption.ownerId, 'owner-a');
  assert.equal(remoteManager.getState('alice').ownsLocation, false);
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
    nativeNode: { routingId: 'node-a' },
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    entryActorRuntime: createEntryActorRuntime(
      () => undefined,
      async (_node, nodeRid, actor) => {
        events.push(`destroyHook:${nodeRid}:${actor.actorId}`);
      }
    )
  });

  await activation.create();
  await activation.notifyCreateActor({ actorId: 'alice' }, framework.ZLinkMessage.from({}));
  await activation.context.destroyActor({ actorId: 'alice' });

  assert.deepEqual(events, [
    'entryCreate:alice',
    'destroyHook:node-a:alice'
  ]);
});

test('ZLinkEntrySpotActivation disposes native resources when onClosing fails', async () => {
  const events = [];
  class EntrySpot {
    async onClosing() {
      events.push('closing');
      throw new Error('entry close failed');
    }
  }
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: {
      routingId: 'entry-stage',
      async dispose() { events.push('native-dispose'); }
    },
    nativeNode: { routingId: 'node-a' },
    nodeRid: 'node-a',
    spotNodeName: 'node-a'
  });

  await activation.create();
  await activation.initialize();
  await assert.rejects(() => activation.dispose(), /entry close failed/);
  await activation.dispose();
  assert.deepEqual(events, ['closing', 'native-dispose']);
});

test('Entry actor commit keeps accepted state while stream binding retries post-commit', async () => {
  const actor = { actorId: 'alice' };
  let attempts = 0;
  let joinedSpotCleared = false;
  let installedRef;
  let remoteTargetCleared = false;
  let resolveRefreshed;
  const refreshed = new Promise((resolve) => { resolveRefreshed = resolve; });
  const state = {
    actor,
    nativeActorRef: { nodeRid: rid('old-node'), actorId: 'alice', generation: 4n },
    clearJoinedSpot() { joinedSpotCleared = true; },
    setNativeActorRef(actorRef) { installedRef = actorRef; }
  };
  const runtime = new ZLinkEntryActorRuntimeService({
    actorManager: () => ({ getState: () => state }),
    spotManager: () => undefined,
    spotNodeRuntime: () => ({ primaryNode: { routingId: rid('entry-node') } }),
    streamBindingRuntime: {
      async refreshActor(actorRef) {
        attempts++;
        if (attempts === 1) throw new Error('binding temporarily unavailable');
        resolveRefreshed(actorRef);
      }
    },
    boundSessionRelay: {
      clearRemoteActorPacketTarget(actorId) {
        remoteTargetCleared = actorId === 'alice';
      }
    }
  });

  await runtime.commitActorTransaction(actor, async () => {});
  const refreshedRef = await Promise.race([
    refreshed,
    new Promise((_, reject) => setTimeout(() => reject(new Error('binding retry timed out')), 500))
  ]);
  assert.equal(attempts, 2);
  assert.equal(joinedSpotCleared, true);
  assert.equal(remoteTargetCleared, true);
  assert.equal(String(installedRef.nodeRid), String(rid('entry-node')));
  assert.equal(refreshedRef, installedRef);
});

// ActorJoinReadable dispatch-event value (core SpotDispatchEvent.ActorJoinReadable = 6).
const ENTRY_ACTOR_JOIN_READABLE = 6;

test('native actor join admission closes caller-owned reply when submit fails', async () => {
  const requestMessage = zlink.Message.from(Buffer.from('join-request'));
  const originalClose = zlink.Message.prototype.close;
  let replyMessage;
  let replyCloseCount = 0;
  zlink.Message.prototype.close = function closeTrackedMessage() {
    if (this === replyMessage) {
      replyCloseCount += 1;
    }
    return originalClose.call(this);
  };

  try {
    const admission = new ZLinkSpotNativeActorJoinAdmission({
      nativeSpot: {
        replyActorJoin() {
          return {
            message(message) {
              replyMessage = message;
              return this;
            },
            submit() {
              throw new Error('actor join reply submit failed');
            }
          };
        }
      },
      serial: {
        execute(action) {
          return Promise.resolve().then(action);
        }
      },
      resolveActor: () => ({ actorId: 'joined-actor' }),
      getTarget: () => ({
        async onActorJoin() {
          return { accepted: true, reply: 'accepted' };
        }
      }),
      defaultAccept: false
    });

    await assert.rejects(
      admission.admit({
        info: { targetActor: { actorId: 'joined-actor' } },
        message: requestMessage
      }),
      /actor join reply submit failed/
    );
    assert.notEqual(replyMessage, undefined);
    assert.equal(replyCloseCount, 1);
  } finally {
    zlink.Message.prototype.close = originalClose;
    requestMessage.close();
  }
});

test('native actor join commits Entry Spot lifecycle after the native reply', async () => {
  const events = [];
  const actor = { actorId: 'alice' };
  const request = zlink.Message.from('return-home');
  const admission = new ZLinkSpotNativeActorJoinAdmission({
    nativeSpot: {
      replyActorJoin(_request, code) {
        assert.equal(code, 0);
        return {
          submit() { events.push('native-reply'); }
        };
      }
    },
    serial: {
      execute(action) { return Promise.resolve().then(action); }
    },
    resolveActor: () => actor,
    getTarget: () => ({
      async onActorJoin() {
        events.push('admit');
        return { accepted: true };
      }
    }),
    defaultAccept: true,
    async commitAcceptedActor(committed) {
      assert.equal(committed, actor);
      events.push('entry-joined');
    }
  });

  await admission.admit({
    info: { targetActor: { actorId: actor.actorId } },
    message: request
  });

  assert.deepEqual(events, ['admit', 'native-reply', 'entry-joined']);
  request.close();
});

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
      await new Promise((resolve) => setImmediate(resolve));
    },
    replies
  };
}

function createEntryActorRuntime(resolveActor, destroyActor = async () => {}) {
  return {
    resolveActor,
    async commitActorTransaction(_actor, onJoined) { await onJoined(); },
    destroyActor,
    async routePacket() {
      return { handled: false };
    }
  };
}

test('ZLinkEntrySpotActivation runs onActorJoin admission on the native dispatch round-trip', async () => {
  const events = [];
  class EntrySpot {
    async onActorJoin(actorId, request) {
      const reason = request.decode();
      events.push(`entryJoin:${actorId}:${reason}`);
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
    nativeNode: { routingId: 'node-a', bindRemoteActorSession() {} },
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    entryActorRuntime: createEntryActorRuntime((actorId) => ({ actorId }))
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

test('native actor join remains accepted when post-commit joined callback throws', async () => {
  class EntrySpot {
    async onActorJoin() { return { accepted: true }; }
    async onJoinedActor() { throw new Error('joined failed'); }
  }
  const harness = createEntryJoinHarness();
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: harness.nativeSpot,
    nativeNode: { routingId: 'node-a', bindRemoteActorSession() {} },
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    entryActorRuntime: createEntryActorRuntime((actorId) => ({ actorId }))
  });
  await activation.create();
  await activation.initialize();

  const request = zlink.Message.from('join');
  harness.enqueue('alice', request);
  await harness.run();

  assert.equal(harness.replies[0].code, 0);
  request.close();
  await activation.dispose();
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
    nativeNode: { routingId: 'node-a', bindRemoteActorSession() {} },
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    entryActorRuntime: createEntryActorRuntime((actorId) => ({ actorId }))
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
    async onActorJoin(actorId) {
      events.push(`entryJoin:${actorId}`);
      return { accepted: true };
    }
  }
  const harness = createEntryJoinHarness();
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: harness.nativeSpot,
    nativeNode: { routingId: 'node-a', bindRemoteActorSession() {} },
    nodeRid: 'node-a',
    spotNodeName: 'node-a',
    entryActorRuntime: createEntryActorRuntime(() => undefined)
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

test('spot actor dispatch rejects malformed JSON as PayloadDecodeFailed before invoking the handler', async () => {
  const errors = [];
  let handlerInvocations = 0;
  class PlayerActor {
    constructor(actorId) {
      this.actorId = actorId;
    }
  }
  class BrokenPayloadHandler {
    async handle() {
      handlerInvocations += 1;
    }
  }
  const actor = new PlayerActor('alice');
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime().addPacket({
    kind: framework.ZLinkActorPacketKind.Send,
    packetName: 'BrokenPayload',
    actorType: PlayerActor,
    handlerType: BrokenPayloadHandler
  });
  const dispatch = new ZLinkSpotActorPacketDispatch({
    spot: {},
    spotRid: () => 'room-1',
    registry,
    resolveActor: () => actor,
    onDisconnectActor: async () => {},
    dispatchErrors: {
      flow: {
        accepts: () => false,
        flowCreationEnabled: () => false
      },
      report(error) {
        errors.push(error);
      }
    }
  });
  const header = zlink.Message.from(Buffer.from(framework.encodeStreamHeader({
    kind: framework.ZLinkStreamMessageKind.Send,
    codec: framework.ZLinkStreamCodec.Json,
    flags: framework.ZLinkStreamHeaderFlags.None,
    name: 'BrokenPayload',
    metadata: new Map()
  })));

  await assert.rejects(
    () => dispatch.dispatch('alice', [header, zlink.Message.from(Buffer.from('{'))]),
    (error) => error instanceof framework.ZLinkFrameworkException
      && error.kind === framework.ZLinkFrameworkErrorKind.PayloadDecodeFailed
  );
  assert.equal(handlerInvocations, 0);
  assert.equal(errors.length, 1);
  assert.equal(errors[0].reason, framework.ZLinkDispatchErrorReason.PayloadDecodeFailed);
  assert.equal(errors[0].action, framework.ZLinkDispatchErrorAction.Drop);
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
      async onActorJoin(actorId, request) {
        events.push(`join:${actorId}:${request.decode()}`);
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
