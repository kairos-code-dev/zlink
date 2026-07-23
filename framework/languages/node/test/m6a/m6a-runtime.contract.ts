import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
  SERVICE_WIRE_MAGIC,
  SERVICE_WIRE_MAJOR,
  SERVICE_WIRE_REQUIRED_CAPABILITY,
  ServiceWireCommand
} from '../../../../runtime/protocol/generated/node/service_wire_constants';
import { SubmitResult } from '@zlink-systems/zlink';
import {
  RawServiceMeshRuntime
} from '../../packages/framework/src/runtime/foundation/raw-service-mesh-runtime';
import {
  ZLinkNodeRawMeshBackend
} from '../../packages/framework/src/runtime/backend/node/node-raw-mesh-backend';
import {
  ServiceDiscoveryRegistry
} from '../../packages/framework/src/runtime/foundation/service-discovery-registry';
import {
  ServiceLivenessRegistry
} from '../../packages/framework/src/runtime/foundation/service-liveness-registry';
import {
  InMemoryServiceLocationAuthority
} from '../../packages/framework/src/runtime/foundation/service-location-authority';
import {
  ServiceMailbox
} from '../../packages/framework/src/runtime/foundation/service-mailbox';
import {
  ServiceTopologyRegistry,
  type ServiceNodeDescriptor
} from '../../packages/framework/src/runtime/foundation/service-topology-registry';
import {
  M6A_SERVICE_WIRE_MAGIC,
  M6A_SERVICE_WIRE_MAJOR,
  M6A_SERVICE_WIRE_REQUIRED_CAPABILITY,
  M6aServiceWireCommand
} from '../../packages/framework/src/runtime/foundation/service-wire-m6a-codec';

function descriptor(
  nodeRoutingId: string,
  endpoint = `inproc://m6a-${nodeRoutingId}-${process.pid}`
): ServiceNodeDescriptor {
  return {
    meshName: 'm6a-mesh',
    nodeRoutingId,
    lifecycleGeneration: 1n,
    descriptorRevision: 1n,
    advertisedEndpoint: endpoint,
    channels: [
      { name: 'alpha', weight: 100 },
      { name: 'beta', weight: 50 }
    ],
    state: 'preparing',
    securityIdentity: 'default',
    effectiveMaxMessageBytes: 4 * 1024 * 1024,
    applicationVersion: 1n,
    protocolCapabilities: ['framework-service-v11'],
    objectRole: 'server',
    placementWeight: 100,
    activeCapacityLimit: 10_000,
    pendingCapacityLimit: 128,
    activeCapacityUsed: 0,
    pendingCapacityUsed: 0
  };
}

test('M6A runtime command subset matches the generated wire schema', () => {
  assert.deepEqual(M6A_SERVICE_WIRE_MAGIC, SERVICE_WIRE_MAGIC);
  assert.equal(M6A_SERVICE_WIRE_MAJOR, SERVICE_WIRE_MAJOR);
  assert.equal(M6A_SERVICE_WIRE_REQUIRED_CAPABILITY, SERVICE_WIRE_REQUIRED_CAPABILITY);
  for (const name of Object.keys(M6aServiceWireCommand) as Array<keyof typeof M6aServiceWireCommand>) {
    assert.equal(M6aServiceWireCommand[name], ServiceWireCommand[name]);
  }
});

test('topology snapshots fence reconnect and exclude retiring placement targets', () => {
  const topology = new ServiceTopologyRegistry(descriptor('local'));
  const peer = { ...descriptor('peer'), state: 'serving' as const };
  assert.equal(topology.admit(peer, 'connection-a'), 'admitted');
  assert.equal(topology.selectChannel('alpha')?.descriptor.nodeRoutingId, 'peer');
  assert.equal(topology.selectPlacement()?.descriptor.nodeRoutingId, 'peer');

  assert.equal(topology.admit(peer, 'connection-b'), 'admitted');
  assert.equal(topology.disconnect('peer', 'connection-a'), false);
  assert.equal(topology.peer('peer')?.connectionId, 'connection-b');

  const conflicting = { ...peer, state: 'retiring' as const };
  assert.equal(topology.admit(conflicting, 'connection-b'), 'staleDescriptor');
  const retiring = { ...conflicting, descriptorRevision: 2n };
  assert.equal(topology.admit(retiring, 'connection-b'), 'admitted');
  assert.equal(topology.selectChannel('alpha'), undefined);
  assert.equal(topology.selectPlacement(), undefined);
});

test('mailbox domains remain bounded and infrastructure claims progress independently', () => {
  const mailbox = new ServiceMailbox({
    applicationMessages: 2,
    applicationBytes: 8,
    infrastructureMessages: 1,
    infrastructureBytes: 8
  });
  assert.equal(mailbox.tryEnqueue({
    owner: 'spot-a',
    domain: 'application',
    parts: [Buffer.from([1, 2, 3])]
  }), true);
  assert.equal(mailbox.tryEnqueue({
    owner: 'spot-a',
    domain: 'application',
    parts: [Buffer.from([4, 5])]
  }), true);
  assert.equal(mailbox.tryEnqueue({
    owner: 'spot-b',
    domain: 'application',
    parts: [Buffer.from([6])]
  }), false);
  assert.equal(mailbox.tryEnqueue({
    owner: 'peer-a',
    domain: 'infrastructure',
    parts: [Buffer.from([9])]
  }), true);

  const application = mailbox.tryClaim('application', 1, 8)!;
  assert.equal(mailbox.tryClaim('application', 1, 8), undefined);
  const infrastructure = mailbox.tryClaim('infrastructure', 1, 8)!;
  assert.equal(infrastructure.records.length, 1);
  assert.equal(mailbox.release(infrastructure), true);
  assert.equal(mailbox.release(infrastructure), false);
  assert.equal(mailbox.release(application), true);
  assert.equal(mailbox.tryClaim('application', 1, 8)?.records.length, 1);
});

test('liveness uses 5s/15s defaults, reuses outstanding probes, and fences old connections', () => {
  const liveness = new ServiceLivenessRegistry();
  liveness.admit('peer', 'connection-a', 0);
  const first = liveness.tick(5_000);
  assert.equal(first.probes.length, 1);
  const probeId = first.probes[0]!.probeId;
  assert.equal(liveness.tick(10_000).probes[0]!.probeId, probeId);
  assert.equal(liveness.acknowledge('peer', 'connection-a', probeId, 10_001), true);

  liveness.admit('peer', 'connection-b', 10_002);
  assert.equal(liveness.disconnect('peer', 'connection-a'), false);
  assert.equal(liveness.acknowledge('peer', 'connection-a', probeId, 10_003), false);
  assert.deepEqual(liveness.tick(25_002).timedOutNodes, ['peer']);
});

test('opaque Location authority performs one CAS publication and emits ordered changes', async () => {
  let now = 1_000;
  const store = new InMemoryServiceLocationAuthority(() => now);
  const changes: Array<{ sequence: bigint; kind: string }> = [];
  const unsubscribe = store.subscribe(change => changes.push(change));
  assert.equal(store.compareExchange(
    'actor/global-a',
    { kind: 'missing' },
    { kind: 'preserve', payload: Buffer.from('invalid') }
  ).kind, 'conflict');
  const first = store.compareExchange(
    'actor/global-a',
    { kind: 'missing' },
    { kind: 'newObject', payload: Buffer.from('owner-a') }
  );
  assert.equal(first.kind, 'stored');
  if (first.kind !== 'stored') return;
  assert.equal(first.objectGeneration, 1n);
  assert.equal(first.authorityOwnerGeneration, 1n);

  const conflict = store.compareExchange(
    'actor/global-a',
    { kind: 'missing' },
    { kind: 'newObject', payload: Buffer.from('loser') }
  );
  assert.equal(conflict.kind, 'conflict');
  now++;
  const moved = store.compareExchange(
    'actor/global-a',
    { kind: 'snapshot', storeVersion: first.storeVersion },
    { kind: 'newOwner', payload: Buffer.from('owner-b') }
  );
  assert.equal(moved.kind, 'stored');
  if (moved.kind !== 'stored') return;
  assert.equal(moved.objectGeneration, first.objectGeneration);
  assert.equal(moved.authorityOwnerGeneration, 2n);
  assert.equal(Buffer.from(moved.payload).toString(), 'owner-b');
  await new Promise(resolve => setImmediate(resolve));
  assert.deepEqual(changes.map(change => ({
    sequence: change.sequence,
    kind: change.kind
  })), [
    { sequence: 1n, kind: 'stored' },
    { sequence: 2n, kind: 'stored' }
  ]);
  unsubscribe();
});

test('ClientServer selection and classic fanout discovery use dedicated descriptor sets', () => {
  const discovery = new ServiceDiscoveryRegistry();
  assert.equal(discovery.admitClientServer({
    channelName: 'orders',
    serverRoutingId: 'server-a',
    lifecycleGeneration: 1n,
    descriptorRevision: 1n,
    weight: 100,
    state: 'serving',
    securityIdentity: 'default',
    effectiveMaxMessageBytes: 1024,
    advertisedEndpoint: 'tcp://server-a:7001'
  }, 'connection-a'), true);
  assert.equal(discovery.selectClientServer('orders')?.serverRoutingId, 'server-a');
  assert.equal(discovery.removeClientServer('orders', 'server-a', 'old-connection'), false);
  assert.equal(discovery.admitClientServer({
    channelName: 'orders',
    serverRoutingId: 'server-a',
    lifecycleGeneration: 1n,
    descriptorRevision: 1n,
    weight: 100,
    state: 'serving',
    securityIdentity: 'default',
    effectiveMaxMessageBytes: 1024,
    advertisedEndpoint: 'tcp://server-a:7001'
  }, 'connection-b'), true);
  assert.equal(discovery.removeClientServer('orders', 'server-a', 'connection-a'), false);

  assert.equal(discovery.admitFanoutPublisher({
    channelName: 'events',
    publisherRoutingId: 'publisher-a',
    lifecycleGeneration: 1n,
    descriptorRevision: 1n,
    advertisedEndpoint: 'tcp://publisher-a:7002',
    state: 'serving'
  }, 'fanout-a'), true);
  assert.deepEqual(
    discovery.fanoutEndpoints('events').map(value => value.publisherRoutingId),
    ['publisher-a']
  );
});

test('raw runtime admits peers and completes node/channel requests once', async () => {
  const endpointNonce = `${process.pid}-${Date.now()}`;
  const leftDescriptor = descriptor('m6a-left', `ipc:///tmp/zlink-m6a-left-${endpointNonce}.sock`);
  const rightDescriptor = descriptor('m6a-right', `ipc:///tmp/zlink-m6a-right-${endpointNonce}.sock`);
  const left = new RawServiceMeshRuntime({ descriptor: leftDescriptor });
  const right = new RawServiceMeshRuntime({ descriptor: rightDescriptor });
  left.start();
  right.start();
  try {
    left.connectPeer(rightDescriptor.advertisedEndpoint, rightDescriptor);
    await pollUntil(() => {
      left.announceExpectedPeers();
      right.pumpOne();
      left.pumpOne();
      return left.topology.peer('m6a-right') !== undefined
        && right.topology.peer('m6a-left') !== undefined;
    });

    assert.equal(left.sendToChannel('alpha', {
      packetName: 'ChannelNotice',
      contentType: 'application/json',
      payload: Buffer.from('notice')
    }), true);
    await pollUntil(() => right.pumpOne() === 'application');
    const sent = right.mailbox.tryClaim('application', 1, 4096)!;
    assert.equal(sent.owner, 'channel:alpha');
    assert.equal(right.mailbox.release(sent), true);

    const pending = left.requestToNode('m6a-right', {
      packetName: 'Question',
      contentType: 'application/json',
      payload: Buffer.from('request')
    }, 2_000);
    await pollUntil(() => right.pumpOne() === 'application');
    const request = right.mailbox.tryClaim('application', 1, 4096)!;
    right.reply(request.records[0]!, {
      packetName: 'Answer',
      contentType: 'application/json',
      payload: Buffer.from('reply')
    });
    assert.equal(right.mailbox.release(request), true);
    const result = await pending.promise;
    assert.equal(result.terminalResult, 0);
    assert.equal(Buffer.from(result.payload!.payload).toString(), 'reply');
  } finally {
    left.close();
    right.close();
  }

  const backend = new ZLinkNodeRawMeshBackend('m6a-mesh', 'backend-host');
  backend.setBind(`ipc:///tmp/zlink-m6a-backend-${process.pid}-${Date.now()}.sock`);
  backend.addChannelName('alpha');
  backend.start();
  try {
    assert.equal(backend.status().state, 2);
    const publisher = backend.createPublisher();
    const publish = await publisher.publishAsync(
      'alpha',
      'topic',
      [Buffer.from('event')]
    );
    assert.equal(publish.result, SubmitResult.Ok);
    assert.equal(publish.detail.snapshotLocalSpotCount, 0);
    publisher.close();
  } finally {
    backend.close();
  }
});

async function pollUntil(condition: () => boolean): Promise<void> {
  const deadline = Date.now() + 2_000;
  while (Date.now() < deadline) {
    if (condition()) return;
    await new Promise(resolve => setTimeout(resolve, 1));
  }
  throw new Error('Timed out waiting for deterministic runtime progress.');
}
