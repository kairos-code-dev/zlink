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
      capabilities: Object.keys(spotNode.actorFactories ?? {})
        .map((actorType) => `actor:${actorType}`)
        .sort(),
      ownerId: '',
      generation: 0n,
      updatedAt: new Date(0)
    },
    executor: new ZLinkSpotNodeAutoConnectExecutor(node, {
      router: hasManualRouterConnections(spotNode),
      pubSub: (spotNode.pubSub?.manualConnections?.length ?? 0) > 0
    })
  };
}

function hasManualRouterConnections(spotNode: ZLinkSpotNodeOptions): boolean {
  return (spotNode.router?.manualConnections?.length ?? 0) > 0
    || (spotNode.router?.manualPeerConnections?.length ?? 0) > 0;
}

class ZLinkSpotNodeAutoConnectExecutor implements IZLinkAutoConnectExecutor {
  constructor(
    private readonly node: ZLinkBackendSpotNode,
    private readonly manualRoles: Readonly<{ router: boolean; pubSub: boolean }>
  ) {}

  connect(target: ZLinkAutoConnectTarget): boolean {
    if (target.connectionKind === 'spot-pub') {
      if (this.manualRoles.pubSub) return false;
      this.node.connectPeer(target.endpoint);
      return true;
    }
    if (target.connectionKind === 'spot-router') {
      if (this.manualRoles.router) return false;
      if (target.nodeRid !== undefined) {
        this.node.connectPeerRid(target.nodeRid, target.endpoint);
      } else {
        this.node.connectPeer(target.endpoint);
      }
      return true;
    }
    return false;
  }

  disconnect(target: ZLinkAutoConnectTarget): void {
    if (target.connectionKind === 'spot-pub') {
      if (this.manualRoles.pubSub) return;
      this.node.disconnectPeer(target.endpoint);
      return;
    }
    if (target.connectionKind === 'spot-router') {
      if (this.manualRoles.router) return;
      if (target.nodeRid !== undefined) {
        this.node.disconnectPeerRid(target.nodeRid);
      } else {
        this.node.disconnectPeer(target.endpoint);
      }
    }
  }

  isDisconnected(target: ZLinkAutoConnectTarget): boolean {
    return !this.node.peers().some((peer) =>
      peer.peerEndpoint === target.endpoint && peer.state === 3);
  }

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
