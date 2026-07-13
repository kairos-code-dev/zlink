'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');
const connector = require('../../packages/stream-connector/dist');

test('DRAIN-001/012 drain is shared, changes readiness, and publishes terminal state', async () => {
  const events = [];
  const publisher = {
    register() {},
    async publish(event) { events.push(event); }
  };
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration(),
    runtimeEventPublisher: publisher
  });
  const waiting = host.awaitDrained();
  const first = host.drain(1000);
  const second = host.drain(10);
  assert.equal(host.isReady(), false);
  assert.deepEqual(await first, { kind: 'drained' });
  assert.deepEqual(await second, { kind: 'drained' });
  assert.deepEqual(await waiting, { kind: 'drained' });
  assert.deepEqual(events.map((event) => event.state), ['Draining', 'Drained']);
  assert(events.every((event) => event.sourceName === 'drain'));
});

test('DRAIN-007 waiter cancellation does not cancel shared drain', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  const controller = new AbortController();
  controller.abort(new Error('wait canceled'));
  await assert.rejects(() => host.drain(1000, controller.signal), /wait canceled/);
  assert.deepEqual(await host.awaitDrained(), { kind: 'drained' });
});

test('DRAIN-006 rejects a non-positive deadline before starting', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  await assert.rejects(() => host.drain(0), /greater than zero/);
  assert.equal(host.isReady(), true);
});

test('drain deadline owns session notification and returns after a hung notification', async () => {
  const host = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  let stops = 0;
  host.streamRuntime = { notifyServerDrain: () => new Promise(() => {}) };
  host.stop = async () => { stops += 1; };
  const started = Date.now();

  assert.deepEqual(await host.drain(20), { kind: 'force-stopped', reason: 'DeadlineExceeded' });
  assert(Date.now() - started < 500);
  assert.equal(stops, 1);
});

test('drain distinguishes marker publication failure from later teardown failure', async () => {
  const markerHost = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  markerHost.locationOwner.runtime = { async publishDraining() { return false; } };
  markerHost.stop = async () => {};
  assert.deepEqual(await markerHost.drain(100), {
    kind: 'force-stopped',
    reason: 'DrainingStatePublishFailed'
  });

  const teardownHost = new framework.ZLinkFrameworkRuntimeHost({
    registration: framework.createFrameworkRegistration()
  });
  teardownHost.streamRuntime = { async notifyServerDrain() { throw new Error('session notification failed'); } };
  teardownHost.stop = async () => {};
  assert.deepEqual(await teardownHost.drain(100), {
    kind: 'force-stopped',
    reason: 'TeardownFailed'
  });
});

test('DRAIN-018 managed stream writes session-closing before disconnecting peer', async () => {
  const order = [];
  let frame;
  const stream = new framework.ZLinkManagedStream({
    send(_routingId, message) {
      order.push('send');
      frame = Uint8Array.from(message.data());
      return true;
    },
    disconnectPeer() { order.push('disconnect'); }
  }, 'session-1');
  await stream.closeForDrain();
  assert.deepEqual(order, ['send', 'disconnect']);
  const decodedFrame = connector.ZlinkStreamFrameCodec.decode(frame);
  const header = connector.ZlinkStreamHeaderCodec.decode(decodedFrame.header);
  assert.equal(header.kind, connector.ZlinkStreamMessageKind.Control);
  assert.equal(header.name, 'session-closing');
  assert.deepEqual([...decodedFrame.payload], [1, 4, 0, 12, ...Buffer.from('server drain')]);
});

test('DRAIN-013 DrainNatural waits for user spots without forcing close', async () => {
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{ name: 'rooms', router: { bind: 'tcp://127.0.0.1:1' }, drainPolicy: 'DrainNatural' }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  let closes = 0;
  const released = createDeferred();
  host.setSpotManager({
    async drainForShutdown() { await released.promise; },
    async close() { closes += 1; return true; }
  });
  const draining = host.drain(1000);
  released.resolve();
  assert.deepEqual(await draining, { kind: 'drained' });
  assert.equal(closes, 0);
});

test('DRAIN-013 DrainNatural retains actors in an active user Spot until natural departure', async () => {
  class RoomSpot {}
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{
      name: 'rooms',
      router: { bind: 'tcp://127.0.0.1:1' },
      drainPolicy: 'DrainNatural',
      spotFactories: [RoomSpot]
    }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  const room = new RoomSpot();
  let active = true;
  let handoffs = 0;
  host.meshRouters.primarySpotMeshName = () => 'rooms';
  host.locationOwner.createRefResolver = () => ({
    async listLivePeers() {
      return [{ nodeRid: 'other-node', draining: false, actorTypes: ['PlayerActor'] }];
    }
  });
  host.setActorManager({
    snapshotStates() {
      return [{
        actor: {
          actorId: 'player-1',
          context: {
            joinEntrySpot() {
              handoffs += 1;
              return { async submit() { return { status: 'accepted' }; } };
            }
          }
        },
        actorType: 'PlayerActor',
        nativeActorRef: { nodeRid: 'local-node', actorId: 'player-1', generation: 1n },
        spotRid: 'room-1',
        spot: room,
        isMoving: false
      }];
    }
  });
  host.setSpotManager({
    retainsActorDuringDrain(spotRid) { return active && spotRid === 'room-1'; },
    async drainForShutdown() {
      if (!active) return;
      await new Promise((resolve) => setTimeout(resolve, 20));
      active = false;
    },
    async close() { throw new Error('DrainNatural must not force-close the room.'); }
  });

  assert.deepEqual(await host.drain(1000), { kind: 'drained' });
  assert.equal(handoffs, 0);
});

test('DRAIN-014 ReleaseAndRecreate closes existing user spots', async () => {
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{ name: 'rooms', router: { bind: 'tcp://127.0.0.1:1' }, drainPolicy: 'ReleaseAndRecreate' }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  let active = true;
  const closed = [];
  host.setSpotManager({
    async drainForShutdown() {
      if (active) {
        closed.push('room-1');
        active = false;
      }
    }
  });
  assert.deepEqual(await host.drain(1000), { kind: 'drained' });
  assert.deepEqual(closed, ['room-1']);
});

test('drain actor handoff delegates target choice to the location placement owner', async () => {
  class PlayerActor {}
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{
      name: 'play',
      router: { bind: 'tcp://127.0.0.1:1' },
      actorFactories: { Player: PlayerActor }
    }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  const placementCalls = [];
  const joins = [];
  host.locationOwner.createRefResolver = () => ({
    async selectActorPlacement(meshName, actorType, sourceNodeRid) {
      placementCalls.push({ meshName, actorType, sourceNodeRid });
      return 'node-target';
    }
  });
  host.setActorManager({
    snapshotStates() {
      return [{
        actor: {
          actorId: 'player-1',
          context: {
            joinEntrySpot(target) {
              joins.push(target);
              return { async submit() { return { status: 'accepted' }; } };
            }
          }
        },
        actorType: 'Player',
        nativeActorRef: { nodeRid: 'node-source', actorId: 'player-1', generation: 1n },
        isMoving: false
      }];
    }
  });
  host.setSpotManager({
    retainsActorDuringDrain() { return false; },
    async drainForShutdown() {}
  });

  assert.deepEqual(await host.drain(1000), { kind: 'drained' });
  assert.deepEqual(placementCalls, [{ meshName: 'play', actorType: 'Player', sourceNodeRid: 'node-source' }]);
  assert.deepEqual(joins, ['node-target']);
});

test('drain retries placement failures until the shared deadline instead of reporting marker failure', async () => {
  class PlayerActor {}
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{
      name: 'play',
      router: { bind: 'tcp://127.0.0.1:1' },
      actorFactories: { Player: PlayerActor }
    }],
    locations: { options: { pollingIntervalMs: 1 } }
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  let placementAttempts = 0;
  host.locationOwner.createRefResolver = () => ({
    async selectActorPlacement() {
      placementAttempts += 1;
      throw new Error('location lookup failed');
    }
  });
  host.setActorManager({ snapshotStates: () => [drainActorState()] });
  host.setSpotManager({
    retainsActorDuringDrain() { return false; },
    async drainForShutdown() {}
  });
  host.stop = async () => {};

  assert.deepEqual(await host.drain(20), { kind: 'force-stopped', reason: 'DeadlineExceeded' });
  assert(placementAttempts > 1);
});

test('drain deadline owns a hung actor placement lookup', async () => {
  class PlayerActor {}
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{
      name: 'play',
      router: { bind: 'tcp://127.0.0.1:1' },
      actorFactories: { Player: PlayerActor }
    }]
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  host.locationOwner.createRefResolver = () => ({
    selectActorPlacement: () => new Promise(() => {})
  });
  host.setActorManager({ snapshotStates: () => [drainActorState()] });
  host.setSpotManager({
    retainsActorDuringDrain() { return false; },
    async drainForShutdown() {}
  });
  host.stop = async () => {};

  assert.deepEqual(await host.drain(20), { kind: 'force-stopped', reason: 'DeadlineExceeded' });
});

test('drain handoff metric counts only accepted actors owned by the drain operation', async () => {
  class PlayerActor {}
  const { provider, records } = metricCollector();
  const registration = framework.createFrameworkRegistration({
    spotNodes: [{
      name: 'play',
      router: { bind: 'tcp://127.0.0.1:1' },
      actorFactories: { Player: PlayerActor }
    }],
    locations: { options: { pollingIntervalMs: 1 } },
    metrics: { meterProvider: provider }
  });
  const host = new framework.ZLinkFrameworkRuntimeHost({ registration });
  let attempts = 0;
  const state = drainActorState(() => ({ status: ++attempts === 1 ? 'rejected' : 'accepted' }));
  host.locationOwner.createRefResolver = () => ({
    async selectActorPlacement() { return 'node-target'; }
  });
  host.setActorManager({ snapshotStates: () => [state] });
  host.setSpotManager({
    retainsActorDuringDrain() { return false; },
    async drainForShutdown() {}
  });
  host.stop = async () => {};

  assert.deepEqual(await host.drain(100), { kind: 'drained' });
  assert.equal(attempts, 2);
  assert.equal(records.filter((record) => record.name === 'zlink.drain.actors.handed_off').length, 1);
});

function drainActorState(result = () => ({ status: 'accepted' })) {
  return {
    actor: {
      actorId: 'player-1',
      context: {
        joinEntrySpot() {
          return { async submit() { return result(); } };
        }
      }
    },
    actorType: 'Player',
    nativeActorRef: { nodeRid: 'node-source', actorId: 'player-1', generation: 1n },
    isMoving: false
  };
}

function metricCollector() {
  const records = [];
  const instrument = (name) => ({
    add(value, attributes) { records.push({ name, value, attributes }); },
    record(value, attributes) { records.push({ name, value, attributes }); }
  });
  return {
    records,
    provider: {
      getMeter() {
        return {
          createCounter: instrument,
          createUpDownCounter: instrument,
          createHistogram: instrument
        };
      }
    }
  };
}

function createDeferred() {
  let resolve;
  const promise = new Promise((complete) => { resolve = complete; });
  return { promise, resolve };
}
