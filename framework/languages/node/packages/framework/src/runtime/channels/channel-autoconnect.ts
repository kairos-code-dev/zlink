import type { RoutingId } from '../../contracts';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type ZLinkLocationChangeStampStore,
  type ZLinkLocationWatchStore,
  type ZLinkLocationOptions,
  type ZLinkPeerLocation
} from '../../contracts';
import type {
  IZLinkAutoConnectExecutor,
  ZLinkAutoConnectLocal,
  ZLinkAutoConnectTarget,
  ZLinkLocationEventSink,
  ZLinkLocationRuntimeStores
} from '../locations';
import {
  ZLinkLocationRuntime,
  ZLinkOwnerLeaseTracker,
  ZLinkStoreLocationResolvers
} from '../locations';
import type {
  ZLinkBackendConnectableSocket,
  ZLinkBackendRouterSocket
} from '../backend/contracts';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';

export interface ZLinkChannelLocationAutoConnectContext {
  readonly runtime: ZLinkLocationRuntime;
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options: ZLinkLocationOptions;
  readonly leaseTracker: ZLinkOwnerLeaseTracker;
  readonly resolver: ZLinkStoreLocationResolvers;
  readonly events?: ZLinkLocationEventSink;
  readonly changeStampStore?: ZLinkLocationChangeStampStore;
  readonly watchStore?: ZLinkLocationWatchStore;
}

export interface ZLinkChannelAutoConnectCapability {
  readonly local: ZLinkAutoConnectLocal;
  readonly localRow?: ZLinkPeerLocation;
  readonly executor: IZLinkAutoConnectExecutor;
  readonly reconcilePeers?: boolean;
}

export function createChannelLocationAutoConnectContext(
  runtime: ZLinkLocationRuntime,
  stores: ZLinkLocationRuntimeStores,
  options: ZLinkLocationOptions,
  events?: ZLinkLocationEventSink
): ZLinkChannelLocationAutoConnectContext {
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

export function buildChannelAutoConnectCapabilities(
  registration: ZLinkFrameworkRegistration,
  sockets: ZLinkChannelSocketRegistry
): ZLinkChannelAutoConnectCapability[] {
  const capabilities: ZLinkChannelAutoConnectCapability[] = [];
  for (const [channelName, channel] of registration.channels.entries()) {
    if (channel.server !== undefined) {
      const endpoint = channel.server.bind ?? '';
      const local = autoConnectLocal(
        ZLinkLocationAutoConnectType.ClientServer,
        channelName,
        ZLinkLocationRole.Router,
        channel.server.routingId,
        endpoint
      );
      capabilities.push({
        local,
        localRow: endpoint.length === 0 ? undefined : peerLocation(local, channel.server.weight),
        executor: new ZLinkSocketAutoConnectExecutor(
          sockets.channelRouter(channelName),
          new Set()
        )
      });
    }
    if (
      channel.client !== undefined
      && (channel.client.manualConnections?.length ?? 0) === 0
    ) {
      const socket = sockets.clientDealer(channelName);
      const local = autoConnectLocal(
        ZLinkLocationAutoConnectType.ClientServer,
        channelName,
        ZLinkLocationRole.Dealer,
        undefined,
        ''
      );
      capabilities.push({
        local,
        executor: new ZLinkSocketAutoConnectExecutor(
          socket,
          new Set(channel.client.manualConnections ?? []),
          {
            monitorDisconnected: (handler) => sockets.monitorDisconnects(socket, handler)
          }
        )
      });
    }
    if (channel.publisher !== undefined) {
      const endpoint = channel.publisher.bind ?? '';
      if (endpoint.length > 0) {
        const local = autoConnectLocal(
          ZLinkLocationAutoConnectType.Fanout,
          channelName,
          ZLinkLocationRole.Pub,
          channel.routingId,
          endpoint
        );
        capabilities.push({
          local,
          localRow: peerLocation(local),
          executor: ZLinkNoopAutoConnectExecutor.instance,
          reconcilePeers: false
        });
      }
    }
    if (
      channel.subscriber !== undefined
      && (channel.subscriber.manualConnections?.length ?? 0) === 0
    ) {
      const socket = sockets.subscriber(channelName);
      const local = autoConnectLocal(
        ZLinkLocationAutoConnectType.Fanout,
        channelName,
        ZLinkLocationRole.Sub,
        channel.routingId,
        ''
      );
      capabilities.push({
        local,
        executor: new ZLinkSocketAutoConnectExecutor(
          socket,
          new Set(channel.subscriber.manualConnections ?? []),
          {
            monitorDisconnected: (handler) => sockets.monitorDisconnects(socket, handler)
          }
        )
      });
    }
  }

  for (const routeChannel of registration.routeChannelOptions.values()) {
    const endpoint = routeChannel.bind ?? '';
    const hasManualConnections = (routeChannel.manualConnections?.length ?? 0) > 0;
    if (hasManualConnections && endpoint.length === 0) {
      continue;
    }
    const local = autoConnectLocal(
      ZLinkLocationAutoConnectType.RouteMesh,
      routeChannel.routerChannelId,
      ZLinkLocationRole.Router,
      routeChannel.routingId,
      endpoint
    );
    capabilities.push({
      local,
      localRow: routeChannel.bind === undefined ? undefined : peerLocation(local, routeChannel.weight),
      executor: hasManualConnections
        ? ZLinkNoopAutoConnectExecutor.instance
        : new ZLinkSocketAutoConnectExecutor(
            sockets.routeRouter(routeChannel.routerChannelId),
            new Set(),
            { routerInitiatorDial: true }
          ),
      reconcilePeers: !hasManualConnections
    });
  }
  return capabilities;
}

class ZLinkSocketAutoConnectExecutor implements IZLinkAutoConnectExecutor {
  private readonly recentlyDisconnected = new Set<string>();
  private readonly pendingReconnects = new Map<string, NodeJS.Immediate>();
  private readonly expectedDisconnects = new Set<string>();
  private disconnectedHandler?: (endpoint: string) => void;

  constructor(
    private readonly socket: ZLinkBackendConnectableSocket,
    private readonly manualEndpoints: ReadonlySet<string>,
    private readonly options: {
      readonly routerInitiatorDial?: boolean;
      readonly monitorDisconnected?: (handler: (endpoint: string) => void) => void;
    } = {}
  ) {
    options.monitorDisconnected?.((endpoint) => this.handleDisconnected(endpoint));
  }

  onDisconnected(handler: (endpoint: string) => void): void {
    this.disconnectedHandler = handler;
  }

  connect(target: ZLinkAutoConnectTarget): boolean {
    if (this.manualEndpoints.has(target.endpoint)) {
      return false;
    }
    if (this.options.routerInitiatorDial === true) {
      configureRouterInitiatorDial(this.socket, target);
    }
    if (this.recentlyDisconnected.delete(target.endpoint)) {
      const pending = setImmediate(() => {
        this.pendingReconnects.delete(target.endpoint);
        this.socket.connect(target.endpoint);
      });
      this.pendingReconnects.set(target.endpoint, pending);
      return true;
    }
    this.socket.connect(target.endpoint);
    return true;
  }

  disconnect(target: ZLinkAutoConnectTarget): void {
    if (this.manualEndpoints.has(target.endpoint)) {
      return;
    }
    const pending = this.pendingReconnects.get(target.endpoint);
    if (pending !== undefined) {
      clearImmediate(pending);
      this.pendingReconnects.delete(target.endpoint);
    } else {
      this.expectedDisconnects.add(target.endpoint);
      this.socket.disconnect(target.endpoint);
      setTimeout(() => this.expectedDisconnects.delete(target.endpoint), 5000).unref();
    }
    this.recentlyDisconnected.add(target.endpoint);
    setImmediate(() => this.recentlyDisconnected.delete(target.endpoint));
  }

  private handleDisconnected(endpoint: string): void {
    if (this.expectedDisconnects.delete(endpoint)) {
      return;
    }
    this.expectedDisconnects.add(endpoint);
    this.socket.disconnect(endpoint);
    setTimeout(() => this.expectedDisconnects.delete(endpoint), 5000).unref();
    this.disconnectedHandler?.(endpoint);
  }
}

class ZLinkNoopAutoConnectExecutor implements IZLinkAutoConnectExecutor {
  static readonly instance = new ZLinkNoopAutoConnectExecutor();

  connect(): boolean { return false; }

  disconnect(): void {}
}

function configureRouterInitiatorDial(socket: ZLinkBackendConnectableSocket, target: ZLinkAutoConnectTarget): void {
  const options = (socket as ZLinkBackendRouterSocket).options;
  if (options === undefined) {
    return;
  }
  if (target.nodeRid !== undefined) {
    options.setConnectRoutingId?.(target.nodeRid);
  }
  if ('probe' in options) {
    options.probe = true;
  }
}

function autoConnectLocal(
  autoConnectType: ZLinkLocationAutoConnectType,
  meshName: string,
  role: ZLinkLocationRole,
  nodeRid: RoutingId | undefined,
  endpoint: string
): ZLinkAutoConnectLocal {
  return {
    autoConnectType,
    meshName,
    role,
    nodeRid,
    endpoint
  };
}

function peerLocation(
  local: ZLinkAutoConnectLocal,
  weight = 100,
  options: {
    readonly metadata?: Readonly<Record<string, string>>;
    readonly capabilities?: readonly string[];
  } = {}
): ZLinkPeerLocation {
  return {
    autoConnectType: local.autoConnectType,
    meshName: local.meshName,
    nodeRid: local.nodeRid,
    role: local.role,
    endpoint: local.endpoint,
    weight,
    draining: false,
    value: 0n,
    metadata: options.metadata,
    capabilities: options.capabilities,
    ownerId: '',
    generation: 0n,
    updatedAt: new Date(0)
  };
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
