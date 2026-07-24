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

test('public weights preserve boundaries, descriptor revisions, ratios, and capacity-first selection', () => {
  const topology = new ServiceTopologyRegistry({
    ...descriptor('local'),
    state: 'serving',
    placementWeight: 0,
    channels: [{ name: 'alpha', weight: 0 }]
  });
  const low = {
    ...descriptor('low'),
    state: 'serving' as const,
    channels: [{ name: 'alpha', weight: 100 }],
    placementWeight: 100
  };
  const high = {
    ...descriptor('high'),
    state: 'serving' as const,
    channels: [{ name: 'alpha', weight: 300 }],
    placementWeight: 300
  };
  const full = {
    ...descriptor('full'),
    state: 'serving' as const,
    channels: [{ name: 'alpha', weight: 0 }],
    placementWeight: 10_000,
    activeCapacityUsed: 10_000
  };
  assert.equal(topology.admit(low, 'low-1'), 'admitted');
  assert.equal(topology.admit(high, 'high-1'), 'admitted');
  assert.equal(topology.admit(full, 'full-1'), 'admitted');

  const channelCounts = new Map<string, number>();
  const placementCounts = new Map<string, number>();
  for (let index = 0; index < 400; index++) {
    const channel = topology.selectChannel('alpha')!.descriptor.nodeRoutingId;
    channelCounts.set(channel, (channelCounts.get(channel) ?? 0) + 1);
    const placement = topology.selectPlacement()!.descriptor.nodeRoutingId;
    placementCounts.set(placement, (placementCounts.get(placement) ?? 0) + 1);
  }
  assert.deepEqual(Object.fromEntries(channelCounts), { high: 300, low: 100 });
  assert.deepEqual(Object.fromEntries(placementCounts), { high: 300, low: 100 });
  assert.equal(channelCounts.has('full'), false);
  assert.equal(placementCounts.has('full'), false);

  const disabledHigh = {
    ...high,
    descriptorRevision: 2n,
    channels: [{ name: 'alpha', weight: 0 }],
    placementWeight: 0
  };
  assert.equal(topology.admit(disabledHigh, 'high-1'), 'admitted');
  assert.equal(topology.selectChannel('alpha')?.descriptor.nodeRoutingId, 'low');
  assert.equal(topology.selectPlacement()?.descriptor.nodeRoutingId, 'low');
  assert.throws(
    () => topology.publishLocal({
      ...topology.localDescriptor(),
      descriptorRevision: 2n,
      placementWeight: -1
    }),
    /0\.\.10000/
  );
  assert.throws(
    () => topology.publishLocal({
      ...topology.localDescriptor(),
      descriptorRevision: 2n,
      channels: [{ name: 'alpha', weight: 10_001 }]
    }),
    /0\.\.10000/
  );
});

test('ClientServer selection uses overflow-safe weights and excludes zero-weight revisions', () => {
  const discovery = new ServiceDiscoveryRegistry();
  const add = (serverRoutingId: string, weight: number, descriptorRevision = 1n) =>
    discovery.admitClientServer({
      channelName: 'orders',
      serverRoutingId,
      lifecycleGeneration: 1n,
      descriptorRevision,
      weight,
      state: 'serving',
      securityIdentity: 'default',
      effectiveMaxMessageBytes: 1024,
      advertisedEndpoint: `tcp://${serverRoutingId}:7001`
    }, `${serverRoutingId}-${descriptorRevision}`);
  assert.equal(add('low', 100), true);
  assert.equal(add('high', 300), true);

  const counts = new Map<string, number>();
  for (let index = 0; index < 400; index++) {
    const selected = discovery.selectClientServer('orders')!.serverRoutingId;
    counts.set(selected, (counts.get(selected) ?? 0) + 1);
  }
  assert.deepEqual(Object.fromEntries(counts), { high: 300, low: 100 });
  assert.equal(add('high', 0, 2n), true);
  assert.equal(discovery.selectClientServer('orders')?.serverRoutingId, 'low');
  assert.throws(() => add('invalid-negative', -1), /0\.\.10000/);
  assert.throws(() => add('invalid-high', 10_001), /0\.\.10000/);
});

test('runtime weight changes increment the local descriptor revision and preserve public bounds', () => {
  const runtime = new RawServiceMeshRuntime({
    descriptor: {
      ...descriptor('runtime-options'),
      state: 'serving'
    }
  });
  const initial = runtime.topology.localDescriptor();
  runtime.updateLocalWeights({ placementWeight: 0 });
  const placement = runtime.topology.localDescriptor();
  assert.equal(placement.descriptorRevision, initial.descriptorRevision + 1n);
  assert.equal(placement.placementWeight, 0);

  runtime.updateLocalWeights({
    channelName: 'alpha',
    channelWeight: 10_000
  });
  const channel = runtime.topology.localDescriptor();
  assert.equal(channel.descriptorRevision, placement.descriptorRevision + 1n);
  assert.equal(channel.channels.find(candidate => candidate.name === 'alpha')?.weight, 10_000);
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
    const initialRevision = backend.status().descriptorRevision;
    backend.setPlacementWeight(0);
    assert.equal(backend.status().descriptorRevision, initialRevision + 1n);
    backend.setChannelWeight('alpha', 0);
    assert.equal(backend.status().descriptorRevision, initialRevision + 2n);
    assert.throws(() => backend.setPlacementWeight(-1), /0\.\.10000/);
    assert.throws(() => backend.setChannelWeight('alpha', 10_001), /0\.\.10000/);
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
