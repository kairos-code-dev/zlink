'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');

test('deployment identity builder publishes validated host-wide version and maintenance wave', () => {
  const registration = framework.createFrameworkRegistrationWithBuilder((options) => {
    options.setApplicationVersion(42n).setMaintenanceWave('blue');
  });
  assert.equal(registration.applicationVersion, 42n);
  assert.equal(registration.maintenanceWave, 'blue');
  assert.equal(framework.createFrameworkRegistration().applicationVersion, 0n);
  assert.throws(
    () => framework.createFrameworkRegistration({ applicationVersion: -1n }),
    /signed 64-bit non-negative range/
  );
  assert.throws(
    () => framework.createFrameworkRegistration({ applicationVersion: 1n << 63n }),
    /signed 64-bit non-negative range/
  );
  assert.throws(
    () => framework.createFrameworkRegistration({ maintenanceWave: '' }),
    /1\.\.255 byte UTF-8/
  );
  assert.throws(
    () => framework.createFrameworkRegistration({ maintenanceWave: 'x'.repeat(256) }),
    /1\.\.255 byte UTF-8/
  );
  assert.throws(
    () => framework.createFrameworkRegistration({ maintenanceWave: 'blue\0wave' }),
    /without NUL/
  );
});

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

test('RouteMesh snapshot projects typed population and activation capacity from the current descriptor', () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const runtime = createRuntime(gate, {
    meshNodeDescriptor: () => ({
      objectRole: framework.ZLinkObjectRole.Server,
      placementWeight: 275,
      populationCapacity: {
        actors: { active: 7, reserved: 2, limit: 100 },
        spots: { active: 3, reserved: 1, limit: 20 },
        spotTypes: [{
          objectKind: 'user_spot',
          stableType: 'room',
          active: 2,
          reserved: 1,
          limit: 10
        }]
      },
      activationConcurrency: { active: 4, limit: 64 },
      applicationVersion: 9n,
      objectCapabilities: [{
        objectKind: 'user_spot',
        stableType: 'room',
        policy: 'snapshot',
        hasSnapshotAdapter: true,
        limit: 10
      }]
    })
  });

  const snapshot = runtime.snapshot('game');
  assert.equal(snapshot.objectRole, framework.ZLinkObjectRole.Server);
  assert.equal(snapshot.placementWeight, 275);
  assert.deepEqual(snapshot.populationCapacity.actors, {
    active: 7,
    reserved: 2,
    limit: 100
  });
  assert.equal(snapshot.populationCapacity.spotTypes[0].stableType, 'room');
  assert.deepEqual(snapshot.activationConcurrency, { active: 4, limit: 64 });
  assert.equal(snapshot.applicationVersion, 9n);
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
    publishRetiring: async () => {},
    rollbackRetiring: async () => {},
    publishDraining: async () => { published += 1; },
    publishHostDraining: async () => {},
    drainResources: async () => { cleaned += 1; },
    cleanupHostResources: async () => {},
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

test('host drain seals every mesh, drains each resource set, and cleans the shared owner once', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const order = [];
  const node = fakeMeshNode();
  const runtime = new framework.ZLinkRouteMeshRuntimeCoordinator({
    meshNames: ['game-a', 'game-b'],
    meshOptions: new Map([['game-a', {}], ['game-b', {}]]),
    meshNode: () => node,
    admission: gate,
    publishRetiring: async (meshName) => { order.push(`retiring:${meshName}`); },
    rollbackRetiring: async (meshName) => { order.push(`rollback:${meshName}`); },
    publishDraining: async (meshName) => { order.push(`publish:${meshName}`); },
    publishHostDraining: async () => { order.push('publish:host'); },
    drainResources: async (meshName) => { order.push(`drain:${meshName}`); },
    cleanupHostResources: async () => { order.push('cleanup'); },
    forceStopResources: async () => {}
  });
  runtime.markServing();

  assert.deepEqual(await runtime.drainHost(), { kind: 'drained' });
  assert.equal(gate.accepts('game-a'), false);
  assert.equal(gate.accepts('game-b'), false);
  assert.equal(runtime.snapshot('game-a').state, framework.ZLinkMeshNodeState.Drained);
  assert.equal(runtime.snapshot('game-b').state, framework.ZLinkMeshNodeState.Drained);
  assert.equal(order.filter((entry) => entry === 'cleanup').length, 1);
  assert.equal(order.filter((entry) => entry === 'publish:host').length, 1);
  assert.deepEqual(
    new Set(order.filter((entry) => entry === 'publish:game-a' || entry === 'publish:game-b')),
    new Set(['publish:game-a', 'publish:game-b'])
  );
  assert.deepEqual(
    new Set(order.filter((entry) => entry.startsWith('drain:'))),
    new Set(['drain:game-a', 'drain:game-b'])
  );
  assert.ok(order.indexOf('retiring:game-a') < order.indexOf('drain:game-a'));
  assert.ok(order.indexOf('drain:game-a') < order.indexOf('publish:game-a'));
  assert.ok(order.indexOf('publish:host') < order.indexOf('cleanup'));
});

test('Retire descriptor publication rolls back before host drain state changes', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const order = [];
  const node = fakeMeshNode();
  const runtime = new framework.ZLinkRouteMeshRuntimeCoordinator({
    meshNames: ['game-a', 'game-b'],
    meshOptions: new Map([['game-a', {}], ['game-b', {}]]),
    meshNode: () => node,
    admission: gate,
    publishRetiring: async (meshName) => {
      order.push(`retiring:${meshName}`);
      if (meshName === 'game-b') throw new Error('store unavailable');
    },
    rollbackRetiring: async (meshName) => { order.push(`serving:${meshName}`); },
    publishDraining: async () => {},
    publishHostDraining: async () => {},
    drainResources: async () => {},
    cleanupHostResources: async () => {},
    forceStopResources: async () => {}
  });
  runtime.markServing();

  assert.equal(await runtime.prepareHostRetire(1000), 'store_unavailable');
  assert.deepEqual(order, [
    'retiring:game-a',
    'retiring:game-b',
    'serving:game-b',
    'serving:game-a'
  ]);
  assert.equal(runtime.isReady('game-a'), true);
  assert.equal(runtime.isReady('game-b'), true);
  assert.equal(gate.accepts('game-a'), true);
  assert.equal(gate.accepts('game-b'), true);
});

test('Retire descriptor publication does not report a reversible block when rollback is unconfirmed', async () => {
  const gate = new framework.ZLinkRuntimeAdmissionGate();
  const runtime = createRuntime(gate, {
    publishRetiring: async () => { throw new Error('ambiguous store response'); },
    rollbackRetiring: async () => { throw new Error('store unavailable'); }
  });

  await assert.rejects(
    () => runtime.prepareHostRetire(1000),
    (error) => error.name === 'ZLinkRetiringRollbackError'
  );
  assert.equal(runtime.isReady('game'), false);
  assert.equal(gate.accepts('game'), true);
});

test('Retire rollback reconciles a committed descriptor when only the Store response was lost', async () => {
  const manager = Object.create(framework.ZLinkSpotNodeRuntimeManager.prototype);
  const committed = {
    rid: 'node-old',
    lifecycleGeneration: 3n,
    descriptorRevision: 8n,
    ownerId: 'owner-old',
    leaseGeneration: 5n
  };
  manager.locationAutoConnect = {
    runtime: {
      currentOwnerToken: { ownerId: 'owner-old', leaseGeneration: 5n },
      async listLiveMeshNodes() { return [committed]; }
    }
  };
  manager.meshNodes = new Map([['game', {
    status() { return { routingId: 'node-old', lifecycleGeneration: 3n }; }
  }]]);
  manager.publishedMeshNodeDescriptors = new Map([['game', {
    ...committed,
    descriptorRevision: 7n
  }]]);
  let republished;
  manager.publishMeshNodeState = async (state, _signal, meshName) => {
    republished = {
      state,
      meshName,
      revision: manager.publishedMeshNodeDescriptors.get(meshName).descriptorRevision
    };
  };

  await manager.reconcileAndPublishMeshNodeState(
    framework.ZLinkFrameworkRuntimeState.Serving,
    'game'
  );

  assert.deepEqual(republished, {
    state: framework.ZLinkFrameworkRuntimeState.Serving,
    meshName: 'game',
    revision: 8n
  });
});

test('Retire readiness requires exact RID and lifecycle generation in the admitted Core peer table', () => {
  const local = {
    routingId: 'node-old',
    lifecycleGeneration: 1n,
    applicationVersion: 1n
  };
  const descriptors = [
    meshDescriptor('node-old', 1n, framework.ZLinkFrameworkRuntimeState.Serving),
    meshDescriptor('node-green', 7n, framework.ZLinkFrameworkRuntimeState.Serving)
  ];

  assert.equal(framework.hasExactPeerReadiness(descriptors, local, [
    { routingId: 'node-green', lifecycleGeneration: 6n, state: 3 }
  ]), false);
  assert.equal(framework.hasExactPeerReadiness(descriptors, local, [
    { routingId: 'node-green', lifecycleGeneration: 7n, state: 2 }
  ]), false);
  assert.equal(framework.hasExactPeerReadiness(descriptors, local, [
    { routingId: 'node-green', lifecycleGeneration: 7n, state: 3 }
  ]), true);
  assert.equal(framework.hasExactPeerReadiness([
    descriptors[0],
    { ...descriptors[1], applicationVersion: 2n }
  ], local, [
    { routingId: 'node-green', lifecycleGeneration: 7n, state: 3 }
  ]), true);
  assert.equal(framework.hasExactPeerReadiness([
    descriptors[0],
    { ...descriptors[1], applicationVersion: 0n }
  ], local, [
    { routingId: 'node-green', lifecycleGeneration: 7n, state: 3 }
  ]), false);
  assert.equal(framework.hasExactPeerReadiness([
    descriptors[0],
    { ...descriptors[1], maintenanceWave: 'wave-a' }
  ], { ...local, maintenanceWave: 'wave-a' }, [
    { routingId: 'node-green', lifecycleGeneration: 7n, state: 3 }
  ]), false);

  const draining = [
    descriptors[0],
    meshDescriptor('node-green', 7n, framework.ZLinkFrameworkRuntimeState.Draining)
  ];
  assert.equal(framework.hasExactPeerReadiness(draining, local, []), false);
  assert.equal(framework.hasExactPeerReadiness([descriptors[0]], local, []), false);
});

function createRuntime(gate, overrides = {}) {
  const node = fakeMeshNode();
  return new framework.ZLinkRouteMeshRuntimeCoordinator({
    meshNames: ['game'],
    meshOptions: new Map([['game', { meshChannels: {} }]]),
    meshNode: (meshName) => meshName === 'game' ? node : undefined,
    meshNodeDescriptor: overrides.meshNodeDescriptor,
    admission: gate,
    publishRetiring: overrides.publishRetiring ?? (async () => {}),
    rollbackRetiring: overrides.rollbackRetiring ?? (async () => {}),
    publishDraining: overrides.publishDraining ?? (async () => {}),
    publishHostDraining: overrides.publishHostDraining ?? (async () => {}),
    drainResources: overrides.drainResources ?? (async () => {}),
    cleanupHostResources: overrides.cleanupHostResources ?? (async () => {}),
    forceStopResources: overrides.forceStopResources ?? (async () => {})
  });
}

function fakeMeshNode() {
  return {
    status() {
      return {
        meshName: 'game', routingId: 'node-a', lifecycleGeneration: 1n,
        descriptorRevision: 1n, localEndpoint: 'tcp://127.0.0.1:1', state: 3,
        lastChangedMs: 1n,
        pendingApplicationMessages: 0n, pendingInfrastructureMessages: 0n
      };
    },
    peers() { return []; },
    peerChannels() { return { names: [], weights: [] }; }
  };
}

function meshDescriptor(rid, lifecycleGeneration, state) {
  return { rid, lifecycleGeneration, state, applicationVersion: 1n };
}

function deferred() {
  let resolve;
  const promise = new Promise((completed) => { resolve = completed; });
  return { promise, resolve };
}

function tick() {
  return new Promise((resolve) => setImmediate(resolve));
}
