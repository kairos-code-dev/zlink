const assert = require('node:assert/strict');
const test = require('node:test');

const { Message } = require('@zlink-systems/zlink');
const {
  DefaultZLinkActorContext
} = require('../../packages/framework/dist/runtime/actors/actor-context');
const {
  deferActorJoin,
  runActorHandlerWithDeferredJoins
} = require('../../packages/framework/dist/runtime/actors/actor-join-deferred-scope');
const {
  ZLinkSpotActorPacketDispatch
} = require('../../packages/framework/dist/runtime/spots/spot-actor-packet-dispatch');
const framework = require('../../packages/framework/dist/internal');
const streamProtocol = require('../../packages/framework/dist/runtime/streams/protocol');
const {
  ZLinkActorRuntimeState
} = require('../../packages/framework/dist/runtime/actors/actor-runtime-state');

function actorHarness(events, completionResult = { accepted: true }) {
  const state = new ZLinkActorRuntimeState('alice');
  const actorRef = {
    nodeRid: 'node-a',
    actorId: 'alice',
    generation: 7n
  };
  const coordinator = {
    async joinSpot(actor, runtimeState, spotId, request, timeoutMs) {
      events.push(`join:${actor.actorId}:${runtimeState.actorId}:${spotId}:${request.data()}:${timeoutMs}`);
      return {
        ...completionResult,
        actor: actorRef,
        reply: Message.from('joined')
      };
    },
    async joinEntrySpot() {
      throw new Error('unexpected Entry Spot join');
    }
  };
  const context = new DefaultZLinkActorContext(
    state,
    coordinator,
    undefined,
    undefined,
    () => 'game',
    undefined
  );
  const actor = {
    actorId: 'alice',
    context,
    async onJoinCompleted(completion) {
      events.push(`completion:${completion.status}:${completion.actor?.generation ?? '-'}`);
    }
  };
  state.bindActor(actor, context);
  return { actor, context };
}

async function waitForEvents(events, count) {
  for (let attempt = 0; attempt < 20 && events.length < count; attempt += 1) {
    await new Promise((resolve) => setImmediate(resolve));
  }
  assert.ok(events.length >= count, `expected at least ${count} events, received ${events.length}`);
}

test('deferred Actor Join starts after the handler continuation and preserves its result', async () => {
  const events = [];
  const { context } = actorHarness(events);

  const result = await runActorHandlerWithDeferredJoins(async () => {
    events.push('handler:start');
    context.joinSpot('room-a', 'hello').timeout(25).defer();
    events.push('handler:end');
    return 'handled';
  });
  assert.equal(result, 'handled');
  assert.equal(events.length, 4);
  const timeoutMs = Number(events[2].split(':').at(-1));
  assert.match(events[2], /^join:alice:alice:room-a:"hello":\d+$/);
  assert.ok(timeoutMs > 0 && timeoutMs <= 25);
  assert.equal(events[3], 'completion:accepted:7');
  assert.equal(context.objectGeneration, 1n);
});

test('Core-routed Actor request submits its reply exactly once before deferred Join completion', async () => {
  const events = [];
  class PlayerActor {
    constructor() {
      this.actorId = 'alice';
    }
  }
  class JoinHandler {
    async handle() {
      events.push('handler');
      deferActorJoin({
        requestBytes: 0,
        discard() {},
        async execute() {
          events.push('completion:0123456789abcdef:fedcba9876543210');
        }
      });
      return { accepted: true };
    }
  }
  const actor = new PlayerActor();
  const registry = new framework.ZLinkSpotActorHandlerRegistryRuntime().addPacket({
    kind: framework.ZLinkActorPacketKind.Request,
    packetName: 'JoinRoom',
    actorType: PlayerActor,
    handlerType: JoinHandler
  });
  const dispatch = new ZLinkSpotActorPacketDispatch({
    spot: { context: { meshName: 'game' } },
    spotId: () => 'entry-a',
    registry,
    resolveActor: () => actor,
    onDisconnectActor: async () => {}
  });
  const parts = [
    Message.from(Buffer.from(streamProtocol.encodeStreamHeader({
      kind: streamProtocol.ZLinkStreamMessageKind.Request,
      codec: streamProtocol.ZLinkStreamCodec.Json,
      flags: streamProtocol.ZLinkStreamHeaderFlags.None,
      requestSeq: 1n,
      name: 'JoinRoom',
      metadata: new Map()
    }))),
    Message.from(Buffer.from('{}'))
  ];
  let replies = 0;

  const result = await dispatch.dispatch(
    'alice',
    parts,
    true,
    undefined,
    undefined,
    (reply) => {
      replies += 1;
      events.push(`reply:${reply.accepted}`);
    }
  );

  assert.equal(result, undefined);
  assert.equal(replies, 1);
  assert.deepEqual(events, [
    'handler',
    'reply:true',
    'completion:0123456789abcdef:fedcba9876543210'
  ]);
});

test('deferred Actor Join is discarded when the handler fails', async () => {
  const events = [];
  const { context } = actorHarness(events);

  await assert.rejects(
    runActorHandlerWithDeferredJoins(async () => {
      context.joinSpot('room-a').defer();
      throw new Error('handler failed');
    }),
    /handler failed/
  );
  assert.deepEqual(events, []);
});

test('Actor Join defer rejects detached use, duplicate terminal, and a second pending transition', async () => {
  const events = [];
  const { context } = actorHarness(events);

  assert.throws(() => context.joinSpot('room-a').defer(), /handler scope/);
  await runActorHandlerWithDeferredJoins(async () => {
    const call = context.joinSpot('room-a');
    call.defer();
    assert.throws(() => call.defer(), (error) => error.kind === 'alreadySubmitted');
    assert.throws(
      () => context.joinSpot('room-b').defer(),
      (error) => error.kind === 'actorMoving'
    );
  });
  await waitForEvents(events, 2);
  const [join, completion] = events.slice(-2);
  assert.match(join, /^join:alice:alice:room-a::\d+$/);
  const remainingTimeout = Number(join.split(':').at(-1));
  assert.ok(remainingTimeout > 0 && remainingTimeout <= 5_000);
  assert.equal(completion, 'completion:accepted:7');
});

test('deferred Actor Join reports an application failure as non-retriable RequestFailed', async () => {
  const events = [];
  const failure = new Error('application admission failed');
  const coordinator = {
    async joinSpot() {
      throw failure;
    },
    async joinEntrySpot() {
      throw new Error('unexpected Entry Spot join');
    }
  };
  const state = new ZLinkActorRuntimeState('bob');
  const failingContext = new DefaultZLinkActorContext(
    state,
    coordinator,
    undefined,
    undefined,
    () => 'game',
    undefined
  );
  const failingActor = {
    actorId: 'bob',
    context: failingContext,
    async onJoinCompleted(completion) {
      events.push(completion);
    }
  };
  state.bindActor(failingActor, failingContext);

  await runActorHandlerWithDeferredJoins(async () => {
    failingContext.joinSpot('room-a').defer();
  });
  await waitForEvents(events, 1);

  assert.equal(events[0].status, 'failed');
  assert.equal(events[0].kind, 'requestFailed');
  assert.equal(events[0].isRetriable, false);
  assert.equal(typeof events[0].operationId.high, 'bigint');
  assert.equal(typeof events[0].operationId.low, 'bigint');
});
