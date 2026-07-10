import type { RoutingId } from '../../contracts';
import {
  ZLinkLocationAutoConnectType,
  ZLinkLocationRole,
  type IZLinkLocationChangeStampStore,
  type IZLinkLocationWatchStore,
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
import { socketTraceId } from './channel-socket-trace';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';

export interface ZLinkChannelLocationAutoConnectContext {
  readonly runtime: ZLinkLocationRuntime;
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options: ZLinkLocationOptions;
  readonly leaseTracker: ZLinkOwnerLeaseTracker;
  readonly resolver: ZLinkStoreLocationResolvers;
  readonly events?: ZLinkLocationEventSink;
  readonly changeStampStore?: IZLinkLocationChangeStampStore;
  readonly watchStore?: IZLinkLocationWatchStore;
}

export interface ZLinkChannelAutoConnectCapability {
  readonly local: ZLinkAutoConnectLocal;
  readonly localRow?: ZLinkPeerLocation;
  readonly executor: IZLinkAutoConnectExecutor;
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
    if (channel.client !== undefined) {
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
          sockets.clientDealer(channelName),
          new Set(channel.client.manualConnections ?? [])
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
          undefined,
          endpoint
        );
        capabilities.push({
          local,
          localRow: peerLocation(local),
          executor: ZLinkNoopAutoConnectExecutor.instance
        });
      }
    }
    if (channel.subscriber !== undefined) {
      const local = autoConnectLocal(
        ZLinkLocationAutoConnectType.Fanout,
        channelName,
        ZLinkLocationRole.Sub,
        undefined,
        ''
      );
      capabilities.push({
        local,
        executor: new ZLinkSocketAutoConnectExecutor(
          sockets.subscriber(channelName),
          new Set(channel.subscriber.manualConnections ?? [])
        )
      });
    }
  }

  for (const routeChannel of registration.routeChannelOptions.values()) {
    const endpoint = routeChannel.bind ?? '';
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
      executor: new ZLinkSocketAutoConnectExecutor(
        sockets.routeRouter(routeChannel.routerChannelId),
        new Set(routeChannel.manualConnections ?? []),
        { routerInitiatorDial: true, routeChannelId: routeChannel.routerChannelId }
      )
    });
  }
  return capabilities;
}

class ZLinkSocketAutoConnectExecutor implements IZLinkAutoConnectExecutor {
  constructor(
    private readonly socket: ZLinkBackendConnectableSocket,
    private readonly manualEndpoints: ReadonlySet<string>,
    private readonly options: { readonly routerInitiatorDial?: boolean; readonly routeChannelId?: string } = {}
  ) {}

  connect(target: ZLinkAutoConnectTarget): boolean {
    if (this.manualEndpoints.has(target.endpoint)) {
      return false;
    }
    if (this.options.routerInitiatorDial === true) {
      configureRouterInitiatorDial(this.socket, target);
    }
    if (process.env.ZLINK_AUTOCONNECT_TRACE === '1') {
      traceAutoConnectSocketDial('connect begin', this.socket, target, this.options.routeChannelId);
    }
    try {
      const result = this.socket.connect(target.endpoint);
      if (process.env.ZLINK_AUTOCONNECT_TRACE === '1') {
        traceAutoConnectSocketDial('connect return', this.socket, target, this.options.routeChannelId, `result=${String(result)}`);
      }
      return true;
    } catch (error) {
      if (process.env.ZLINK_AUTOCONNECT_TRACE === '1') {
        traceAutoConnectSocketDial('connect error', this.socket, target, this.options.routeChannelId, formatAutoConnectError(error));
      }
      throw error;
    }
  }

  disconnect(target: ZLinkAutoConnectTarget): void {
    if (this.manualEndpoints.has(target.endpoint)) {
      return;
    }
    this.socket.disconnect(target.endpoint);
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

function traceAutoConnectSocketDial(
  event: string,
  socket: ZLinkBackendConnectableSocket,
  target: ZLinkAutoConnectTarget,
  routeChannelId: string | undefined,
  detail?: string
): void {
  console.error(
    `[zlink-autoconnect] socket ${event} id=${socketTraceId(socket)} channel=${routeChannelId ?? '<none>'} ` +
    `rid=${formatAutoConnectSocketRid(target.nodeRid)} endpoint=${target.endpoint}` +
    (detail === undefined ? '' : ` ${detail}`)
  );
}

export function traceRouteMeshSocket(
  event: string,
  routeChannelId: string | undefined,
  socket: ZLinkBackendConnectableSocket,
  targetNodeRid?: string
): void {
  if (process.env.ZLINK_AUTOCONNECT_TRACE !== '1') {
    return;
  }
  console.error(
    `[zlink-autoconnect] socket ${event} id=${socketTraceId(socket)} channel=${routeChannelId ?? '<none>'}` +
    (targetNodeRid === undefined ? '' : ` target=${targetNodeRid}`)
  );
}

function formatAutoConnectSocketRid(rid: RoutingId | undefined): string {
  if (rid === undefined) {
    return '<none>';
  }
  const value = rid as unknown as { toHex?: () => string };
  if (typeof value.toHex === 'function') {
    return value.toHex.call(rid).toLowerCase();
  }
  return Buffer.from(rid, 'utf8').toString('hex');
}

function formatAutoConnectError(error: unknown): string {
  if (error instanceof Error) {
    return `${error.name}: ${error.message}`;
  }
  return String(error);
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
    value: 0n,
    metadata: options.metadata,
    capabilities: options.capabilities,
    ownerId: '',
    generation: 0n,
    updatedAt: new Date(0)
  };
}

function isLocationChangeStampStore(value: unknown): value is IZLinkLocationChangeStampStore {
  return value !== null
    && typeof value === 'object'
    && typeof (value as { getChangeStamp?: unknown }).getChangeStamp === 'function';
}

function isLocationWatchStore(value: unknown): value is IZLinkLocationWatchStore {
  return value !== null
    && typeof value === 'object'
    && typeof (value as { watch?: unknown }).watch === 'function';
}
