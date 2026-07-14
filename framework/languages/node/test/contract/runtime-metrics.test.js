'use strict';

const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');
const connector = require('../../packages/stream-connector/dist');
const channelEnvelope = require('../../packages/framework/dist/runtime/channels/channel-envelope');

function collector() {
  const records = [];
  const instrument = (name, kind) => ({
    add(value, attributes) { records.push({ name, kind, value, attributes }); },
    record(value, attributes) { records.push({ name, kind, value, attributes }); }
  });
  return {
    records,
    provider: {
      getMeter(name) {
        assert.equal(name, 'zlink.framework');
        return {
          createCounter: (instrumentName) => instrument(instrumentName, 'counter'),
          createUpDownCounter: (instrumentName) => instrument(instrumentName, 'updown'),
          createHistogram: (instrumentName) => instrument(instrumentName, 'histogram')
        };
      }
    }
  };
}

test('RMETRIC-001 global OpenTelemetry no-op provider remains callable', () => {
  const metrics = new framework.ZLinkRuntimeMetrics();
  assert.equal(metrics.enabled(), true);
  metrics.count('zlink.fanout.published');
  metrics.change('zlink.channel.request.inflight', 1);
  metrics.duration('zlink.channel.request.duration', 0.01);
});

test('RMETRIC-006 framework metrics use stable catalog names and closed labels', () => {
  const { provider, records } = collector();
  const metrics = new framework.ZLinkRuntimeMetrics(provider);
  metrics.change('zlink.channel.request.inflight', 1, { channel: 'api' });
  metrics.count('zlink.channel.request.timeouts', 1, { channel: 'api' });
  metrics.duration('zlink.channel.request.duration', 0.125, { channel: 'api' });
  assert.deepEqual(records.map(({ name, kind }) => ({ name, kind })), [
    { name: 'zlink.channel.request.inflight', kind: 'updown' },
    { name: 'zlink.channel.request.timeouts', kind: 'counter' },
    { name: 'zlink.channel.request.duration', kind: 'histogram' }
  ]);
  assert(records.every((record) => record.attributes.channel === 'api'));
});

test('RMETRIC-016 connector owns reconnect attempt counting', async () => {
  const { provider, records } = collector();
  let attempts = 0;
  const instance = connector.zlinkStreamConnectorFactory.create({
    endpoint: 'ws://127.0.0.1:7999',
    meterProvider: provider,
    reconnect: { enabled: true, maxAttempts: 3, initialDelayMs: 1, maxDelayMs: 1 },
    transportFactory: {
      async connect() {
        attempts += 1;
        if (attempts < 3) throw new Error('retry');
        return { async write() {}, async close() {} };
      }
    }
  });
  await instance.connect();
  await instance.close();
  assert.equal(attempts, 3);
  assert.equal(records.filter((record) => record.name === 'zlink.stream.reconnects').length, 2);
});

test('RMETRIC Entry Spot activation records entry count and lifecycle counters', async () => {
  const { provider, records } = collector();
  const metrics = new framework.ZLinkRuntimeMetrics(provider);
  class EntrySpot {
    async onJoinedActor() {}
    async onLeaveActor() {}
  }
  const activation = new framework.ZLinkEntrySpotActivation({
    entrySpotType: EntrySpot,
    nativeSpot: {
      routingId: 'entry-spot',
      async dispose() {}
    },
    nativeNode: { routingId: 'node-1' },
    nodeRid: 'node-1',
    spotNodeName: 'play',
    metrics
  });

  await activation.create();
  await activation.configure();
  await activation.initialize();
  await activation.dispose();

  assert.deepEqual(records.map(({ name, kind, value, attributes }) => ({
    name,
    kind,
    value,
    attributes
  })), [
    { name: 'zlink.spot.count', kind: 'updown', value: 1, attributes: { kind: 'entry' } },
    { name: 'zlink.spot.created', kind: 'counter', value: 1, attributes: { kind: 'entry' } },
    { name: 'zlink.spot.count', kind: 'updown', value: -1, attributes: { kind: 'entry' } },
    { name: 'zlink.spot.closed', kind: 'counter', value: 1, attributes: { kind: 'entry' } }
  ]);
});

test('RMETRIC channel fanout receive omits unregistered dynamic topic labels', async () => {
  const { provider, records } = collector();
  const metrics = new framework.ZLinkRuntimeMetrics(provider);
  const dispatcher = new framework.ZLinkChannelPublishDispatcher({
    channelName: 'events',
    dispatchErrors: new framework.ZLinkDispatchErrorReporter(
      undefined,
      undefined,
      { reportRuntimeTaskException() {} }
    ),
    handlers: new Map([
      ['DynamicEvent', { async handle() {} }]
    ]),
    metrics
  });
  const parts = channelEnvelope.encodeChannelEnvelopeParts(
    4,
    'events',
    'DynamicEvent',
    { value: 'payload' },
    undefined,
    'room.user-supplied-123'
  ).map((part) => ({
    data: () => Buffer.from(part)
  }));

  await dispatcher.dispatch({ topic: 'room.user-supplied-123', parts });

  const received = records.find((record) => record.name === 'zlink.fanout.received');
  assert.equal(received.value, 1);
  assert.equal(received.attributes, undefined);
});

test('OBS-B2/B3 runtime metric catalog keeps stable instrument kinds and low-cardinality labels', () => {
  const { provider, records } = collector();
  const metrics = new framework.ZLinkRuntimeMetrics(provider);
  for (const name of [
    'zlink.stream.connections.opened', 'zlink.stream.connections.closed',
    'zlink.spot.created', 'zlink.spot.closed', 'zlink.actor.transfers',
    'zlink.channel.request.timeouts', 'zlink.channel.messages.dropped',
    'zlink.fanout.published', 'zlink.fanout.received',
    'zlink.location.store.errors', 'zlink.location.owner_lease.renew.failures',
    'zlink.location.write.conflicts', 'zlink.observability.observer.overflow',
    'zlink.drain.actors.handed_off', 'zlink.drain.rooms.drained', 'zlink.drain.forced'
  ]) metrics.count(name, 1, { outcome: 'success' });
  for (const name of [
    'zlink.stream.connections.active', 'zlink.spot.count', 'zlink.spot.queue.depth',
    'zlink.actor.count', 'zlink.actor.mailbox.depth', 'zlink.channel.request.inflight',
    'zlink.location.peers', 'zlink.drain.state'
  ]) metrics.change(name, 1, { state: 'serving' });
  for (const name of [
    'zlink.stream.handshake.duration', 'zlink.stream.session.bind.duration',
    'zlink.spot.queue.wait.duration', 'zlink.spot.timer.tick.lateness',
    'zlink.actor.transfer.duration', 'zlink.channel.request.duration',
    'zlink.location.owner_lease.renew.lateness', 'zlink.drain.duration'
  ]) metrics.duration(name, 0.001);
  metrics.histogram('zlink.actor.transfer.pending_requests.count', 0, '{request}');
  const names = new Set(records.map((record) => record.name));
  assert.equal(names.size, records.length);
  assert(records.every((record) => !record.attributes ||
    !['actor_id', 'spot_rid', 'flow_id', 'correlation_id'].some((key) => key in record.attributes)));
});
