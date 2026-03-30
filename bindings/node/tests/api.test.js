'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('service objects expose aligned monitor and query surface', () => {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const discovery = new zlink.Discovery(ctx, zlink.ServiceType.SPOT, 'svc');
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(ctx);
  const query = new zlink.RegistryQueryClient(ctx);

  registry.bind('inproc://registry-pub', 'inproc://registry-router');
  query.connect('inproc://registry-router');
  discovery.setValue(7);
  discovery.setMetadata(Buffer.from('meta'));

  const discoveryMonitor = discovery.openMonitor();
  const nodeMonitor = node.openMonitor();
  const spotMonitor = spot.openMonitor();

  assert.equal(registry.statusSnapshot().topologyEntryCount, 0);
  assert.deepEqual(registry.serviceSummarySnapshot(), []);
  assert.deepEqual(registry.topologySnapshot(), []);
  assert.deepEqual(registry.topologyQuery(), []);
  assert.deepEqual(query.snapshot(), []);
  assert.equal(discovery.value(), 7);
  assert.equal(discovery.metadata().toString(), 'meta');
  assert.deepEqual(discovery.memberPeers(), []);
  assert.equal(discovery.receiverCount(), 0);
  assert.equal(discovery.serviceAvailable(), false);
  assert.equal(discovery.getReceivers, undefined);
  assert.equal(node.peersSnapshot().length, 0);
  assert.equal(node.subjectsSnapshot().length, 0);
  assert.equal(typeof discoveryMonitor.recv, 'function');
  assert.equal(typeof nodeMonitor.close, 'function');
  assert.equal(typeof spotMonitor.close, 'function');
  assert.throws(() => registry.bind('inproc://registry-pub-2', 'inproc://registry-router-2'), /only be called once/);
  assert.throws(() => registry.setEndpoints('inproc://legacy-pub', 'inproc://legacy-router'), /removed/);
  assert.throws(() => registry.start(), /removed/);
  assert.throws(
    () => registry.setSockOpt(zlink.RegistrySocketRole.PUB, zlink.SocketOption.LINGER, Buffer.alloc(4)),
    /not available on the aligned public API/
  );
  assert.throws(
    () => node.setDiscovery(discovery),
    /Use attachDiscovery/
  );

  spotMonitor.close();
  nodeMonitor.close();
  discoveryMonitor.close();
  query.close();
  spot.close();
  node.close();
  discovery.close();
  registry.close();
  ctx.close();
});

test('removed receiver stays removed from aligned api', () => {
  assert.equal('Receiver' in zlink, false);
  assert.equal(zlink.Receiver, undefined);
});

test('discovery requires a service name in the aligned api', () => {
  const ctx = new zlink.Context();
  assert.throws(() => new zlink.Discovery(ctx, zlink.ServiceType.SPOT), /serviceName/);
  assert.throws(() => new zlink.Discovery(ctx, zlink.ServiceType.SPOT, ''), /serviceName/);
  ctx.close();
});
