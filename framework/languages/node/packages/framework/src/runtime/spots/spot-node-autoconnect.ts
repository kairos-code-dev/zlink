import type { ZLinkLocationOptionOverrides } from '../../contracts/Locations/Options';
import type {
  ZLinkLocationChangeStampStore,
  ZLinkLocationWatchStore,
  ZLinkPeerLocation
} from '../../contracts';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole
} from '../../contracts';
import type { ZLinkSpotNodeOptions } from '../configuration';
import type { ZLinkBackendMeshNode } from '../backend/contracts';
import { toBindingRoutingId } from '../routing-id';
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
  readonly options: ZLinkLocationOptionOverrides;
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

export function createSpotNodeLocationAutoConnectContext(
  runtime: ZLinkLocationRuntime,
  stores: ZLinkLocationRuntimeStores,
  options: ZLinkLocationOptionOverrides,
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
  node: ZLinkBackendMeshNode
): ZLinkSpotNodeAutoConnectCapability | undefined {
  if (spotNode.router === undefined) {
    return undefined;
  }
  // The node has already started when this capability is built. Publishing
  // the configured bind string would advertise port 0 for wildcard binds,
  // so peers must receive the concrete endpoint resolved by Core.
  const status = node.status();
  const endpoint = status.localEndpoint;
  const local: ZLinkAutoConnectLocal = {
    autoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
    meshName: spotNodeName,
    role: ZLinkLocationRole.Router,
    nodeRid: String(status.routingId),
    endpoint
  };
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
      capabilities: Object.keys(spotNode.actorFactories ?? {})
        .map((actorType) => `actor:${actorType}`)
        .sort(),
      ownerId: '',
      generation: 0n,
      updatedAt: new Date(0)
    },
    executor: new ZLinkSpotNodeAutoConnectExecutor(
      node,
      hasManualRouterConnections(spotNode)
    )
  };
}

function hasManualRouterConnections(spotNode: ZLinkSpotNodeOptions): boolean {
  return (spotNode.router?.manualConnections?.length ?? 0) > 0
    || (spotNode.router?.manualPeerConnections?.length ?? 0) > 0;
}

class ZLinkSpotNodeAutoConnectExecutor implements IZLinkAutoConnectExecutor {
  private readonly connectionIntents = new Map<string, bigint>();

  constructor(
    private readonly node: ZLinkBackendMeshNode,
    private readonly manualConnections: boolean
  ) {}

  connect(target: ZLinkAutoConnectTarget): boolean {
    if (this.manualConnections) return false;
    this.connectPeer(target);
    return true;
  }

  disconnect(target: ZLinkAutoConnectTarget): void {
    if (this.manualConnections) return;
    this.disconnectPeer(target);
  }

  isDisconnected(target: ZLinkAutoConnectTarget): boolean {
    return !this.node.peers().some((peer) =>
      peer.routingId !== null &&
      peer.endpoint === target.endpoint &&
      (target.nodeRid === undefined || String(peer.routingId) === String(target.nodeRid)) &&
      peer.state === 3);
  }

  private connectPeer(target: ZLinkAutoConnectTarget): void {
    const key = connectionKey(target);
    if (this.connectionIntents.has(key)) {
      return;
    }
    const connectionIntentId = this.node.connectPeer({
      endpoint: target.endpoint,
      expectedRid: target.nodeRid === undefined
        ? undefined
        : toBindingRoutingId(target.nodeRid)
    });
    this.connectionIntents.set(key, connectionIntentId);
  }

  private disconnectPeer(target: ZLinkAutoConnectTarget): void {
    const key = connectionKey(target);
    const connectionIntentId = this.connectionIntents.get(key);
    if (connectionIntentId !== undefined) {
      this.node.removePeerConnection(connectionIntentId);
      this.connectionIntents.delete(key);
    }
    for (const peer of this.node.peers()) {
      if (peer.routingId === null) {
        continue;
      }
      if (peer.endpoint !== target.endpoint) {
        continue;
      }
      if (target.nodeRid !== undefined && String(peer.routingId) !== String(target.nodeRid)) {
        continue;
      }
      this.node.disconnectPeer(peer.routingId, peer.lifecycleGeneration);
    }
  }
}

function connectionKey(target: ZLinkAutoConnectTarget): string {
  return `${target.nodeRid ?? ''}\0${target.endpoint}`;
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
