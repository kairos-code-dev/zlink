import type {
  ZLinkLocationChangeStampStore,
  ZLinkLocationWatchStore,
  ZLinkLocationOptions,
  ZLinkPeerLocation
} from '../../contracts';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole
} from '../../contracts';
import type { ZLinkSpotNodeOptions } from '../configuration';
import type { ZLinkBackendSpotNode } from '../backend/contracts';
import {
  ZLinkLocationRuntime,
  ZLinkOwnerLeaseTracker,
  ZLinkStoreLocationResolvers,
  type IZLinkAutoConnectExecutor,
  type ZLinkAutoConnectLocal,
  type ZLinkAutoConnectTarget,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';

export interface ZLinkSpotNodeLocationAutoConnectContext {
  readonly runtime: ZLinkLocationRuntime;
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options: ZLinkLocationOptions;
  readonly leaseTracker: ZLinkOwnerLeaseTracker;
  readonly resolver: ZLinkStoreLocationResolvers;
  readonly events?: ZLinkLocationEventSink;
  readonly changeStampStore?: ZLinkLocationChangeStampStore;
  readonly watchStore?: ZLinkLocationWatchStore;
}

export interface ZLinkSpotNodeAutoConnectCapability {
  readonly local: ZLinkAutoConnectLocal;
  readonly localRow: ZLinkPeerLocation;
  readonly executor: IZLinkAutoConnectExecutor;
}

const SPOT_PUB_ENDPOINT_METADATA_KEY = 'pub-endpoint';

export function createSpotNodeLocationAutoConnectContext(
  runtime: ZLinkLocationRuntime,
  stores: ZLinkLocationRuntimeStores,
  options: ZLinkLocationOptions,
  events?: ZLinkLocationEventSink
): ZLinkSpotNodeLocationAutoConnectContext {
  const leaseTracker = new ZLinkOwnerLeaseTracker({
    store: stores.ownerLeaseStore,
    options
  });
  return {
    runtime,
    stores,
    options,
    leaseTracker,
    resolver: new ZLinkStoreLocationResolvers({
      stores,
      leaseTracker,
      events
    }),
    events,
    changeStampStore: isLocationChangeStampStore(stores.peerStore) ? stores.peerStore : undefined,
    watchStore: isLocationWatchStore(stores.peerStore) ? stores.peerStore : undefined
  };
}

export function spotNodeAutoConnectCapability(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions,
  node: ZLinkBackendSpotNode
): ZLinkSpotNodeAutoConnectCapability | undefined {
  if (spotNode.router === undefined) {
    return undefined;
  }
  const endpoint = spotNode.router.bind ?? '';
  const local: ZLinkAutoConnectLocal = {
    autoConnectType: ZLinkLocationAutoConnectType.SpotMesh,
    meshName: spotNodeName,
    role: ZLinkLocationRole.Spot,
    nodeRid: node.routingId,
    endpoint
  };
  const metadata = spotNode.pubSub?.bind === undefined
    ? undefined
    : { [SPOT_PUB_ENDPOINT_METADATA_KEY]: spotNode.pubSub.bind };
  return {
    local,
    localRow: {
      autoConnectType: local.autoConnectType,
      meshName: local.meshName,
      nodeRid: local.nodeRid,
      role: local.role,
      endpoint,
      weight: 100,
      draining: false,
      value: 0n,
      metadata,
      ownerId: '',
      generation: 0n,
      updatedAt: new Date(0)
    },
    executor: new ZLinkSpotNodeAutoConnectExecutor(node, spotNodeManualEndpoints(spotNode))
  };
}

function spotNodeManualEndpoints(spotNode: ZLinkSpotNodeOptions): ReadonlySet<string> {
  return new Set([
    ...(spotNode.router?.manualConnections ?? []),
    ...(spotNode.router?.manualPeerConnections ?? []).map((connection) => connection.endpoint),
    ...(spotNode.pubSub?.manualConnections ?? [])
  ]);
}

class ZLinkSpotNodeAutoConnectExecutor implements IZLinkAutoConnectExecutor {
  constructor(
    private readonly node: ZLinkBackendSpotNode,
    private readonly manualEndpoints: ReadonlySet<string>
  ) {}

  connect(target: ZLinkAutoConnectTarget): boolean {
    let connected = false;
    if (!this.manualEndpoints.has(target.endpoint)) {
      if (target.nodeRid !== undefined) {
        this.node.connectPeerRid(target.nodeRid, target.endpoint);
      } else {
        this.node.connectPeer(target.endpoint);
      }
      connected = true;
    }
    const pubEndpoint = spotPubEndpointOf(target);
    if (pubEndpoint !== undefined && !this.manualEndpoints.has(pubEndpoint)) {
      this.node.connectPeer(pubEndpoint);
      connected = true;
    }
    return connected;
  }

  disconnect(target: ZLinkAutoConnectTarget): void {
    if (!this.manualEndpoints.has(target.endpoint)) {
      this.node.disconnectPeer(target.endpoint);
    }
    const pubEndpoint = spotPubEndpointOf(target);
    if (pubEndpoint !== undefined && !this.manualEndpoints.has(pubEndpoint)) {
      this.node.disconnectPeer(pubEndpoint);
    }
  }
}

function spotPubEndpointOf(target: ZLinkAutoConnectTarget): string | undefined {
  const endpoint = target.metadata?.[SPOT_PUB_ENDPOINT_METADATA_KEY];
  return endpoint === undefined || endpoint.length === 0 ? undefined : endpoint;
}

function isLocationChangeStampStore(value: unknown): value is ZLinkLocationChangeStampStore {
  return value !== null
    && typeof value === 'object'
    && typeof (value as { getChangeStamp?: unknown }).getChangeStamp === 'function';
}

function isLocationWatchStore(value: unknown): value is ZLinkLocationWatchStore {
  return value !== null
    && typeof value === 'object'
    && typeof (value as { watch?: unknown }).watch === 'function';
}
