import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type IZLinkLocationStore,
  type ZLinkPeerLocation
} from '@zlink-systems/framework';
import { SampleNames } from '../../Shared/Configuration/sample-names';
import { createDeliveryDispatchLocationStore } from '../Configuration/location-store';
import type { DeliveryDispatchServerConfig } from '../Configuration/sample-config';

const pubEndpointMetadataKey = 'pub-endpoint';

type ExpectedPeer = {
  readonly label: string;
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly endpoints: readonly string[];
};

async function waitForTopology(config: DeliveryDispatchServerConfig, timeoutMs: number): Promise<void> {
  const store = createDeliveryDispatchLocationStore(config);
  try {
    await waitForExpectedPeers(store, expectedPeers(config), timeoutMs);
    console.log('topology=ready');
  } finally {
    await store.dispose();
  }
}

async function waitForExpectedPeers(
  store: IZLinkLocationStore,
  expected: readonly ExpectedPeer[],
  timeoutMs: number
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  let lastMissing: readonly string[] = [];
  while (Date.now() < deadline) {
    lastMissing = await missingPeers(store, expected);
    if (lastMissing.length === 0) {
      return;
    }
    await delay(100);
  }
  throw new Error(`DeliveryDispatch topology was not ready: ${lastMissing.join(', ')}`);
}

async function missingPeers(store: IZLinkLocationStore, expected: readonly ExpectedPeer[]): Promise<readonly string[]> {
  const leases = await store.listOwnerLeases();
  const liveOwnerIds = new Set(
    leases.leases
      .filter((lease) => lease.leaseExpiresAt.getTime() > leases.storeNow.getTime())
      .map((lease) => lease.ownerId)
  );
  const missing: string[] = [];
  for (const peer of expected) {
    const rows = await store.listPeers({
      autoConnectType: peer.autoConnectType,
      meshName: peer.meshName,
      role: peer.role
    });
    const liveRows = rows.filter((row) => liveOwnerIds.has(row.ownerId));
    for (const endpoint of peer.endpoints) {
      if (!liveRows.some((row) => rowProvidesEndpoint(row, endpoint))) {
        missing.push(`${peer.label}@${endpoint}`);
      }
    }
  }
  return missing;
}

function rowProvidesEndpoint(row: ZLinkPeerLocation, endpoint: string): boolean {
  return row.endpoint === endpoint || row.metadata?.[pubEndpointMetadataKey] === endpoint;
}

function expectedPeers(config: DeliveryDispatchServerConfig): readonly ExpectedPeer[] {
  return [
    {
      label: 'dispatch-router',
      autoConnectType: ZLinkLocationAutoConnectType.ClientServer,
      meshName: SampleNames.dispatchChannel,
      role: ZLinkLocationRole.Router,
      endpoints: [config.dispatchEndpoint]
    },
    {
      label: 'courier-router',
      autoConnectType: ZLinkLocationAutoConnectType.ClientServer,
      meshName: SampleNames.courierRouteChannel,
      role: ZLinkLocationRole.Router,
      endpoints: [config.courierRouteEndpoint]
    },
    {
      label: 'tracking-router',
      autoConnectType: ZLinkLocationAutoConnectType.ClientServer,
      meshName: SampleNames.trackingChannel,
      role: ZLinkLocationRole.Router,
      endpoints: [config.trackingEndpoint]
    },
    {
      label: 'status-publisher',
      autoConnectType: ZLinkLocationAutoConnectType.Fanout,
      meshName: SampleNames.statusFanoutChannel,
      role: ZLinkLocationRole.Pub,
      endpoints: [config.statusFanoutEndpoint]
    },
    {
      label: 'courier-actor-route-router',
      autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
      meshName: SampleNames.courierActorNodeRouteChannel,
      role: ZLinkLocationRole.Router,
      endpoints: [config.courierActorNode1RouteEndpoint, config.courierActorNode2RouteEndpoint]
    },
    {
      label: 'courier-actor-spot',
      autoConnectType: ZLinkLocationAutoConnectType.SpotMesh,
      meshName: SampleNames.courierActorSpotMesh,
      role: ZLinkLocationRole.Spot,
      endpoints: [config.courierActorNode1SpotEndpoint, config.courierActorNode2SpotEndpoint]
    },
    {
      label: 'delivery-spot',
      autoConnectType: ZLinkLocationAutoConnectType.SpotMesh,
      meshName: SampleNames.deliverySpotMesh,
      role: ZLinkLocationRole.Spot,
      endpoints: [
        config.trackingSpotRouterEndpoint,
        config.trackingSpotEndpoint,
        config.sessionSpotRouterEndpoint,
        config.sessionSpotEndpoint
      ]
    }
  ];
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export { waitForTopology };
