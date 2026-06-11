const assert = require('node:assert/strict');
const test = require('node:test');

const framework = require('../../packages/framework/dist/internal');

test('socket monitoring source maps backend raw events into framework typed events', async () => {
  const events = [];
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkSocketMonitoringSource(
    {
      sourceName: 'api.server',
      events: [framework.ZLinkSocketEventKind.ConnectionReady]
    },
    fakeSocketMonitor(),
    publisher
  );

  await source.publish({
    nativeEvent: framework.ZLinkSocketNativeEventType.Connected,
    routingId: 'peer-a',
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://remote',
    value: 1
  });
  await source.publish({
    nativeEvent: framework.ZLinkSocketNativeEventType.ConnectionReady,
    routingId: 'peer-a',
    localAddr: 'tcp://local',
    remoteAddr: 'tcp://remote',
    value: 2
  });

  assert.equal(events.length, 1);
  assert.equal(events[0].sourceName, 'api.server');
  assert.equal(events[0].event, framework.ZLinkSocketEventKind.ConnectionReady);
  assert.equal(events[0].diagnostic.nativeEvent, framework.ZLinkSocketNativeEventType.ConnectionReady);
  assert.equal(events[0].diagnostic.nativeValue, 2);
});

test('registry monitoring source publishes snapshot changes and suppresses unchanged polls', async () => {
  const events = [];
  let readyCount = 1;
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkRegistryMonitoringSource(
    { sourceName: 'registry', intervalMs: 1000 },
    {
      async status() {
        return {
          registryId: 1,
          bindEndpoint: 'tcp://registry',
          state: framework.ZLinkRegistryState.Active,
          topologyEntryCount: 1,
          peerRegistryCount: 0,
          connectedPeerRegistryCount: 0,
          listSeq: BigInt(readyCount),
          lastError: 0,
          lastChangedMs: 10n
        };
      },
      async topology() {
        return [topologyEntry(readyCount)];
      },
      async serviceSummary() {
        return [{
          autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
          serviceRole: framework.ZLinkServiceRole.Router,
          channelName: 'api',
          totalCount: 1,
          connectingCount: 0,
          readyCount,
          errorCount: 0,
          stoppedCount: 0,
          lastReportedMs: 20n
        }];
      },
      async memberPeers() {
        return [];
      }
    },
    publisher
  );

  await source.pollOnce();
  await source.pollOnce();
  readyCount = 2;
  await source.pollOnce();

  assert.deepEqual(events.map((event) => event.event), [
    framework.ZLinkRegistryEventKind.StatusChanged,
    framework.ZLinkRegistryEventKind.TopologyChanged,
    framework.ZLinkRegistryEventKind.ServiceSummaryChanged,
    framework.ZLinkRegistryEventKind.StatusChanged,
    framework.ZLinkRegistryEventKind.TopologyChanged,
    framework.ZLinkRegistryEventKind.ServiceSummaryChanged
  ]);
  assert.equal(events[2].serviceSummary[0].readyCount, 1);
  assert.equal(events[5].serviceSummary[0].readyCount, 2);
});

test('spot monitoring source publishes status peers and subjects snapshot changes', async () => {
  const events = [];
  let connectedPeerCount = 1;
  const publisher = new framework.DefaultZLinkRuntimeEventPublisher();
  publisher.register({ async handle(event) { events.push(event); } });
  const source = new framework.ZLinkSpotMonitoringSource(
    { sourceName: 'stage-node', intervalMs: 1000 },
    {
      status() {
        return spotStatus(connectedPeerCount);
      },
      peers() {
        return [spotPeer(connectedPeerCount)];
      },
      subjects() {
        return [spotSubject(connectedPeerCount)];
      }
    },
    publisher
  );

  await source.pollOnce();
  await source.pollOnce();
  connectedPeerCount = 2;
  await source.pollOnce();

  assert.deepEqual(events.map((event) => event.event), [
    framework.ZLinkSpotEventKind.StatusChanged,
    framework.ZLinkSpotEventKind.PeersChanged,
    framework.ZLinkSpotEventKind.SubjectsChanged,
    framework.ZLinkSpotEventKind.StatusChanged,
    framework.ZLinkSpotEventKind.PeersChanged,
    framework.ZLinkSpotEventKind.SubjectsChanged
  ]);
  assert.equal(events[0].status.connectedPeerCount, 1);
  assert.equal(events[3].status.connectedPeerCount, 2);
  assert.equal(events[4].peers[0].state, framework.ZLinkSpotPeerState.Connected);
  assert.equal(events[5].subjects[0].readyPeerCount, 2);
});

function fakeSocketMonitor() {
  return {
    nativeInstance: {},
    onEvent() {},
    recv() {},
    async dispose() {}
  };
}

function topologyEntry(readyCount) {
  return {
    autoConnectType: framework.ZLinkAutoConnectType.ClientServer,
    routingId: 'peer-a',
    serviceKind: framework.ZLinkServiceKind.Socket,
    serviceRole: framework.ZLinkServiceRole.Router,
    channelName: 'api',
    endpoint: 'tcp://peer:7101',
    source: framework.ZLinkTopologySource.Registry,
    state: framework.ZLinkTopologyState.Ready,
    desiredCount: 1,
    readyCount,
    errorCode: 0,
    lastReportedMs: 20n,
    spotKind: framework.ZLinkSpotKind.User
  };
}

function spotStatus(connectedPeerCount) {
  return {
    channelName: 'game.stage',
    localEndpoint: 'tcp://stage',
    nodeRoutingId: 'stage-node',
    state: framework.ZLinkSpotNodeState.Ready,
    configuredPeerCount: 2,
    activePeerCount: connectedPeerCount,
    connectedPeerCount,
    subjectCount: 1,
    readySubjectCount: 1,
    lastError: 0,
    lastChangedMs: BigInt(connectedPeerCount)
  };
}

function spotPeer(connectedPeerCount) {
  return {
    channelName: 'game.stage',
    localEndpoint: 'tcp://stage',
    peerEndpoint: `tcp://peer-${connectedPeerCount}`,
    source: framework.ZLinkSpotPeerSource.Manual,
    kind: framework.ZLinkSpotPeerKind.SpotMesh,
    state: framework.ZLinkSpotPeerState.Connected,
    weight: 1,
    connectedSinceMs: 1n,
    lastChangedMs: BigInt(connectedPeerCount)
  };
}

function spotSubject(readyPeerCount) {
  return {
    role: framework.ZLinkSpotRole.Sub,
    subject: 'room.*',
    subjectKind: framework.ZLinkSubjectKind.Pattern,
    readyPeerCount,
    activePeerCount: readyPeerCount,
    lastChangedMs: BigInt(readyPeerCount)
  };
}
