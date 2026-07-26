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

test('Framework runtime termination surface is concrete and Nest exports topology tokens', async () => {
  const host = new internal.ZLinkFrameworkRuntimeHost({
    registration: internal.createFrameworkRegistration()
  });
  const events = host.observe()[Symbol.asyncIterator]();
  const result = await host.shutdown({ deadlineMs: 1000 });
  const event = await events.next();

  assert.equal(result.outcome, framework.ZLinkTerminationOutcome.Stopped);
  assert.equal(host.snapshot().terminalResult.outcome, framework.ZLinkTerminationOutcome.Stopped);
  assert.equal(event.value.identifier, 'zlink.runtime.host.termination_changed');
  assert.equal(typeof nestjs.ZLINK_CLIENT_SERVER_RUNTIME, 'symbol');
  assert.equal(typeof nestjs.ZLINK_FANOUT_RUNTIME, 'symbol');
});
