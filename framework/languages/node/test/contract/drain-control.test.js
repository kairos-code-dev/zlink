'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');

test('fixed drain seals admission, publishes draining, waits accepted work, then drains resources', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const order = [];
  const release = deferred();
  const runtime = createRuntime(gate, {
    async publishDraining() { order.push('published'); },
    async drainResources() { order.push('resources'); }
  });
  runtime.markServing();
  const accepted = gate.run('game', 'accepted request', async () => {
    order.push('handler');
    await release.promise;
    order.push('reply_closed');
  });

  const draining = runtime.drain('game', 1000);
  await tick();
  assert.deepEqual(order, ['handler', 'published']);
  assert.equal(runtime.isReady('game'), false);
  assert.throws(
    () => gate.claim('game', 'late request'),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RequestRejected
  );

  release.resolve();
  await accepted;
  assert.deepEqual(await draining, { kind: 'drained' });
  assert.deepEqual(order, ['handler', 'published', 'reply_closed', 'resources']);
});

test('drain and awaitDrained share one mesh-keyed operation', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  let calls = 0;
  const runtime = createRuntime(gate, {
    async drainResources() { calls += 1; }
  });
  runtime.markServing();
  const waiting = runtime.awaitDrained('game');
  const first = runtime.drain('game');
  const second = runtime.drain('game', 1);
  assert.deepEqual(await first, { kind: 'drained' });
  assert.deepEqual(await second, { kind: 'drained' });
  assert.deepEqual(await waiting, { kind: 'drained' });
  assert.equal(calls, 1);
});

test('deadline uses the closed snake_case force reason and terminal event exactly once', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const runtime = createRuntime(gate, {
    async drainResources(_meshName, signal) {
      await new Promise((_, reject) => signal.addEventListener('abort', () => reject(signal.reason), { once: true }));
    }
  });
  runtime.markServing();
  const events = [];
  const observed = (async () => {
    for await (const event of runtime.observe('game', 4)) events.push(event);
  })();
  assert.deepEqual(await runtime.drain('game', 10), {
    kind: 'forceStopped',
    reason: 'deadline_exceeded'
  });
  await observed;
  assert.deepEqual(events.map((event) => event.state), [
    framework.ZLinkMeshNodeState.Draining,
    framework.ZLinkMeshNodeState.ForceStopping
  ]);
  assert.equal(events.filter((event) => event.state === framework.ZLinkMeshNodeState.ForceStopping).length, 1);
});

test('drain classifies publish, owner cleanup, and teardown failures with closed snake_case reasons', async () => {
  const cases = [
    ['ZLinkDrainingStatePublishError', 'drain_state_publish_failed', 'publishDraining'],
    ['ZLinkOwnerCleanupError', 'owner_cleanup_failed', 'drainResources'],
    ['Error', 'teardown_failed', 'drainResources']
  ];
  for (const [errorName, reason, phase] of cases) {
    const gate = new framework.ZLinkRuntimeAdmissionGate();
    const failure = new Error(reason);
    failure.name = errorName;
    const runtime = createRuntime(gate, {
      async publishDraining() {
        if (phase === 'publishDraining') throw failure;
      },
      async drainResources() {
        if (phase === 'drainResources') throw failure;
      }
    });
    assert.deepEqual(await runtime.drain('game'), { kind: 'forceStopped', reason });
  }
});

test('stale or unknown mesh handles fail with a typed route error and do not create state', () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const runtime = createRuntime(gate);
  assert.throws(
    () => runtime.snapshot('missing'),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected
  );
  assert.throws(
    () => runtime.isReady('missing'),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RouteNotConnected
  );
});

test('multi-mesh drain fails before global owner cleanup can mutate another mesh', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  let published = 0;
  let cleaned = 0;
  const node = fakeMeshNode();
  const runtime = new framework.ZLinkRouteMeshRuntimeCoordinator({
    meshNames: ['game-a', 'game-b'],
    meshOptions: new Map([['game-a', {}], ['game-b', {}]]),
    meshNode: () => node,
    admission: gate,
    publishDraining: async () => { published += 1; },
    drainResources: async () => { cleaned += 1; },
    forceStopResources: async () => {}
  });

  await assert.rejects(
    () => runtime.drain('game-a'),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RequestRejected
  );
  await assert.rejects(
    () => runtime.awaitDrained('game-b'),
    (error) => error.kind === framework.ZLinkFrameworkErrorKind.RequestRejected
  );
  assert.equal(published, 0);
  assert.equal(cleaned, 0);
  assert.equal(gate.accepts('game-a'), true);
  assert.equal(gate.accepts('game-b'), true);
});

function createRuntime(gate, overrides = {}) {
  const node = fakeMeshNode();
  return new framework.ZLinkRouteMeshRuntimeCoordinator({
    meshNames: ['game'],
    meshOptions: new Map([['game', { meshChannels: {} }]]),
    meshNode: (meshName) => meshName === 'game' ? node : undefined,
    admission: gate,
    publishDraining: overrides.publishDraining ?? (async () => {}),
    drainResources: overrides.drainResources ?? (async () => {}),
    forceStopResources: overrides.forceStopResources ?? (async () => {})
  });
}

function fakeMeshNode() {
  return {
    status() {
      return {
        meshName: 'game', routingId: 'node-a', lifecycleGeneration: 1n,
        descriptorRevision: 1n, localEndpoint: 'tcp://127.0.0.1:1', state: 3,
        lastChangedMs: 1n, multicastSubmitted: 0n, multicastDroppedTargets: 0n,
        pendingApplicationMessages: 0n, pendingInfrastructureMessages: 0n
      };
    },
    peers() { return []; },
    peerChannels() { return { names: [], weights: [] }; }
  };
}

function deferred() {
  let resolve;
  const promise = new Promise((completed) => { resolve = completed; });
  return { promise, resolve };
}

function tick() {
  return new Promise((resolve) => setImmediate(resolve));
}
