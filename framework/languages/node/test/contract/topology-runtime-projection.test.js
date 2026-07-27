const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist');
const internal = require('../../packages/framework/dist/internal');
const nestjs = require('../../packages/nestjs/dist');

test('ClientServer runtime projects live topology and emits bounded change events', async () => {
  let changed;
  const manager = {
    clientServerTopology() {
      return {
        localRole: 'clientAndServer',
        pendingRequestCount: 2,
        descriptors: [{
          channelName: 'orders',
          serverRoutingId: 'server-a',
          lifecycleGeneration: 3n,
          descriptorRevision: 5n,
          weight: 100,
          state: 'serving',
          securityIdentity: 'default',
          effectiveMaxMessageBytes: 1024,
          advertisedEndpoint: 'tcp://127.0.0.1:10000'
        }]
      };
    },
    observeClientServerTopology(_channelName, callback) {
      changed = callback;
      return () => { changed = undefined; };
    }
  };
  const runtime = new internal.ZLinkClientServerRuntimeProjection(() => manager);
  const snapshot = runtime.snapshot('orders');

  assert.equal(snapshot.localRole, 'clientAndServer');
  assert.equal(snapshot.selectable, true);
  assert.equal(snapshot.readyServerCount, 1);
  assert.equal(snapshot.pendingRequestCount, 2);
  assert.equal(snapshot.servers[0].serverRid, 'server-a');

  const events = runtime.observe('orders')[Symbol.asyncIterator]();
  changed();
  const event = await events.next();
  assert.equal(event.value.channelName, 'orders');
  assert.equal(event.value.ready, true);
  await events.return();
  assert.equal(changed, undefined);
});

test('Fanout runtime projects publisher readiness and emits the exact event union', async () => {
  let changed;
  const manager = {
    fanoutTopology() {
      return {
        descriptors: [{
          channelName: 'events',
          publisherRoutingId: 'publisher-a',
          lifecycleGeneration: 7n,
          descriptorRevision: 9n,
          advertisedEndpoint: 'tcp://127.0.0.1:10001',
          state: 'serving'
        }]
      };
    },
    observeFanoutTopology(_channelName, callback) {
      changed = callback;
      return () => { changed = undefined; };
    }
  };
  const runtime = new internal.ZLinkFanoutRuntimeProjection(() => manager);
  const snapshot = runtime.snapshot('events');

  assert.equal(snapshot.readyConnectionCount, 1);
  assert.equal(snapshot.publishers[0].state, 'ready');

  const events = runtime.observe('events')[Symbol.asyncIterator]();
  changed();
  const event = await events.next();
  assert.equal(event.value.identifier, 'zlink.runtime.fanout.publisher_changed');
  assert.equal(event.value.entry.publisherRid, 'publisher-a');
  await events.return();
});

test('Framework runtime shutdown surface emits status and Nest exports topology tokens', async () => {
  const host = new internal.ZLinkFrameworkRuntimeHost({
    registration: internal.createFrameworkRegistration()
  });
  const events = host.observe()[Symbol.asyncIterator]();
  const result = await host.shutdown({ deadlineMs: 1000 });
  const event = await events.next();

  assert.equal(result.outcome, framework.ZLinkFrameworkTerminationOutcome.Stopped);
  assert.equal(
    host.status.terminationResult.outcome,
    framework.ZLinkFrameworkTerminationOutcome.Stopped
  );
  assert.equal(event.value.state, framework.ZLinkFrameworkRuntimeState.Draining);
  assert.equal(typeof nestjs.ZLINK_CLIENT_SERVER_RUNTIME, 'symbol');
  assert.equal(typeof nestjs.ZLINK_FANOUT_RUNTIME, 'symbol');
});

test('Relocate rejects local manual topology before changing host state and Shutdown remains available', async () => {
  const registration = internal.createFrameworkRegistration({
    channels: {
      orders: { client: { manualConnections: ['tcp://127.0.0.1:19001'] } }
    }
  });
  const host = new internal.ZLinkFrameworkRuntimeHost({ registration });

  // The focused contract test enters the observable Serving state without
  // starting transport resources; the blocker must run before touching them.
  host.runtimeState = framework.ZLinkFrameworkRuntimeState.Serving;
  assert.deepEqual(await host.relocate({
    mode: framework.ZLinkFrameworkRelocationMode.PlannedMaintenance
  }), {
    mode: framework.ZLinkFrameworkRelocationMode.PlannedMaintenance,
    effectiveTargetApplicationVersion: 0n,
    outcome: framework.ZLinkFrameworkRelocationOutcome.Blocked,
    reason: framework.ZLinkFrameworkRelocationReason.ManualTopologyUnsupported
  });
  assert.equal(host.status.state, framework.ZLinkFrameworkRuntimeState.Serving);
  assert.equal(host.status.acceptingWork, true);
  assert.equal(host.status.relocationResult, undefined);

  const shutdown = await host.shutdown({ deadlineMs: 1000 });
  assert.equal(shutdown.outcome, framework.ZLinkFrameworkTerminationOutcome.Stopped);
});

test('Relocate keeps Serving when descriptor publication is reversibly rolled back', async () => {
  const host = new internal.ZLinkFrameworkRuntimeHost({
    registration: internal.createFrameworkRegistration()
  });
  host.executionState = {};
  host.runtimeState = framework.ZLinkFrameworkRuntimeState.Serving;
  host.routeMeshCoordinator = {
    async prepareHostRetire() { return 'store_unavailable'; }
  };

  assert.deepEqual(await host.relocate({
    mode: framework.ZLinkFrameworkRelocationMode.PlannedMaintenance
  }), {
    mode: framework.ZLinkFrameworkRelocationMode.PlannedMaintenance,
    effectiveTargetApplicationVersion: 0n,
    outcome: framework.ZLinkFrameworkRelocationOutcome.Blocked,
    reason: framework.ZLinkFrameworkRelocationReason.StoreUnavailable
  });
  assert.equal(host.status.state, framework.ZLinkFrameworkRuntimeState.Serving);
  assert.equal(host.status.relocationResult, undefined);
});

test('Relocate reports an irreversible descriptor rollback failure without claiming success', async () => {
  const host = new internal.ZLinkFrameworkRuntimeHost({
    registration: internal.createFrameworkRegistration()
  });
  host.executionState = {};
  host.runtimeState = framework.ZLinkFrameworkRuntimeState.Serving;
  host.routeMeshCoordinator = {
    async prepareHostRetire() { throw new internal.ZLinkRetiringRollbackError(); }
  };
  host.stop = async () => {};

  const result = await host.relocate({
    mode: framework.ZLinkFrameworkRelocationMode.PlannedMaintenance
  });
  assert.equal(result.outcome, framework.ZLinkFrameworkRelocationOutcome.Blocked);
  assert.equal(result.reason, framework.ZLinkFrameworkRelocationReason.RelocationFailed);
  assert.equal(host.status.state, framework.ZLinkFrameworkRuntimeState.Error);
});

test('Relocation manual topology classification covers every local service registration', () => {
  const manualRegistrations = [
    { routeChannels: [{ routerChannelId: 'route-a', bind: 'tcp://127.0.0.1:19101', manualConnections: ['tcp://127.0.0.1:19001'] }] },
    { spotNodes: { play: { router: { bind: 'tcp://127.0.0.1:19102', manualConnections: ['tcp://127.0.0.1:19002'] } } } },
    { spotNodes: { play: { router: { bind: 'tcp://127.0.0.1:19103', manualPeerConnections: [{ peerRid: 'peer-a', endpoint: 'tcp://127.0.0.1:19003' }] } } } },
    { channels: { orders: { client: { manualConnections: ['tcp://127.0.0.1:19004'] } } } },
    { channels: { events: {
      subscriber: { manualConnections: ['tcp://127.0.0.1:19005'] },
      publishHandlers: [{ packetName: 'Event', handler: { async handle() {} } }]
    } } },
    { channels: { events: { publisher: { bind: 'tcp://127.0.0.1:19006' } } } }
  ];

  for (const options of manualRegistrations) {
    const registration = internal.createFrameworkRegistration(options);
    assert.equal(internal.hasUnsupportedManualTopology(registration), true);
  }

  const automaticPublisher = internal.createFrameworkRegistration({
    locations: { useInMemoryStores: true },
    channels: { events: { publisher: { bind: 'tcp://127.0.0.1:19007' } } }
  });
  assert.equal(internal.hasUnsupportedManualTopology(automaticPublisher), false);
});

test('Relocation requires explicit valid mode and rolling update target version', async () => {
  const host = new internal.ZLinkFrameworkRuntimeHost({
    registration: internal.createFrameworkRegistration({ applicationVersion: 3n })
  });
  assert.throws(
    () => host.relocate({}),
    /mode is required/
  );
  assert.throws(
    () => host.relocate({
      mode: framework.ZLinkFrameworkRelocationMode.PlannedMaintenance,
      targetApplicationVersion: 4n
    }),
    /cannot define targetApplicationVersion/
  );
  assert.throws(
    () => host.relocate({
      mode: framework.ZLinkFrameworkRelocationMode.RollingUpdate,
      targetApplicationVersion: 3n
    }),
    /greater than the source version/
  );
});

test('Successful relocation leaves infrastructure started until explicit shutdown', async () => {
  const host = new internal.ZLinkFrameworkRuntimeHost({
    registration: internal.createFrameworkRegistration({ applicationVersion: 3n })
  });
  host.executionState = {};
  host.runtimeState = framework.ZLinkFrameworkRuntimeState.Serving;
  host.routeMeshCoordinator = {
    async prepareHostRetire() { return 'prepared'; },
    async drainHost() { return { kind: 'drained' }; }
  };

  const result = await host.relocate({
    mode: framework.ZLinkFrameworkRelocationMode.RollingUpdate,
    targetApplicationVersion: 4n
  });
  assert.deepEqual(result, {
    mode: framework.ZLinkFrameworkRelocationMode.RollingUpdate,
    effectiveTargetApplicationVersion: 4n,
    outcome: framework.ZLinkFrameworkRelocationOutcome.Relocated,
    reason: framework.ZLinkFrameworkRelocationReason.None
  });
  assert.equal(host.status.state, framework.ZLinkFrameworkRuntimeState.Relocated);
  assert.equal(host.isStarted, true);
});
