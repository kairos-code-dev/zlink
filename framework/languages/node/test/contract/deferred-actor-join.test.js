const assert = require('node:assert/strict');
const test = require('node:test');

const { Message } = require('@zlink-systems/zlink');
const {
  DefaultZLinkActorContext
} = require('../../packages/framework/dist/runtime/actors/actor-context');
const {
  runActorHandlerWithDeferredJoins
} = require('../../packages/framework/dist/runtime/actors/actor-join-deferred-scope');
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

test('deferred Actor Join starts only after a normal handler terminal and reports completion', async () => {
  const events = [];
  const { context } = actorHarness(events);

  await runActorHandlerWithDeferredJoins(async () => {
    events.push('handler:start');
    context.joinSpot('room-a', 'hello').timeout(25).defer();
    events.push('handler:end');
  });

  assert.deepEqual(events.slice(0, 2), ['handler:start', 'handler:end']);
  const timeoutMs = Number(events[2].split(':').at(-1));
  assert.match(events[2], /^join:alice:alice:room-a:"hello":\d+$/);
  assert.ok(timeoutMs > 0 && timeoutMs <= 25);
  assert.equal(events[3], 'completion:accepted:7');
  assert.equal(context.objectGeneration, 1n);
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
  assert.deepEqual(events.slice(-2), [
    'join:alice:alice:room-a::5000',
    'completion:accepted:7'
  ]);
});
