import type {
  ZLinkLocationRuntimeQuery,
  ZLinkActorLocation,
  ZLinkActorLocationKey,
  ZLinkLocationKey,
  ZLinkAutoConnectDesiredSetChange,
  ZLinkLocationActorEvent,
  ZLinkLocationMonitoringRegistration,
  ZLinkLocationPeerEvent,
  ZLinkPeerLocation,
  ZLinkLocationRouteEvent,
  ZLinkLocationRuntimeEvent,
  ZLinkLocationRuntimeEventKind,
  ZLinkLocationRuntimeStatus,
  ZLinkLocationTopologyEntry,
  ZLinkLocationServiceSummary,
  ZLinkLocationSpotEvent,
  ZLinkPollingMonitoringRegistration,
  ZLinkRouteLocation,
  ZLinkRouteLocationKey,
  ZLinkRuntimeEvent,
  ZLinkRuntimeEventHandler,
  ZLinkRuntimeEventPublisher as ZLinkRuntimeEventPublisherContract,
  ZLinkSocketEvent,
  ZLinkSocketEventKind,
  ZLinkSocketMonitoringRegistration,
  ZLinkSocketNativeEventType,
  ZLinkSpotLocation,
  ZLinkSpotLocationKey,
  ZLinkMeshNodeSnapshot,
  ZLinkMeshPeerSnapshot,
  ZLinkSpotEvent,
} from '../../contracts';
import {
  ZLinkLocationActorEventKind as ActorLocationEventKind,
  ZLinkLocationPeerEventKind as PeerLocationEventKind,
  ZLinkLocationRouteEventKind as RouteLocationEventKind,
  ZLinkLocationRuntimeEventKind as LocationRuntimeEventKind,
  ZLinkLocationSpotEventKind as SpotLocationEventKind,
  ZLinkSocketEventKind as SocketEventKind,
  ZLinkSocketNativeEventType as SocketNativeEventType,
  ZLinkMeshNodeState,
  ZLinkObjectRole,
  ZLinkSpotEventKind as SpotEventKind
} from '../../contracts';
import type {
  ZLinkBackendMeshNode,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSocketMonitorEvent
} from '../backend';
import type { ZLinkSpotNodeOptions } from '../configuration';
import { normalizeOpaqueRoutingId } from '../routing-id';

const ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED = 3;

export class DefaultZLinkRuntimeEventPublisher implements ZLinkRuntimeEventPublisherContract {
  private readonly handlers: ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>[] = [];

  register<TEvent extends ZLinkRuntimeEvent>(handler: ZLinkRuntimeEventHandler<TEvent>): void {
    this.handlers.push(handler as ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>);
  }

  async publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void> {
    for (const handler of this.handlers) {
      try {
        await handler.handle(event);
      } catch (error) {
        console.error('[monitoring-event-dispatch]', error);
        continue;
      }
    }
  }
}

export class ZLinkSocketMonitoringSource {
  private readonly enabledEvents: ReadonlySet<ZLinkSocketEventKind> | undefined;

  constructor(
    private readonly registration: ZLinkSocketMonitoringRegistration,
    private readonly monitor: ZLinkBackendSocketMonitor,
    private readonly publisher: ZLinkRuntimeEventPublisherContract
  ) {
    validateSourceName(registration.sourceName);
    this.enabledEvents = registration.events === undefined
      ? undefined
      : new Set(registration.events);
  }

  start(): void {
    this.monitor.onEvent((event) => {
      void this.publish(event);
    });
  }

  publish(raw: ZLinkBackendSocketMonitorEvent): Promise<void> {
    const event = toSocketEvent(this.registration.sourceName, raw);
    if (event === undefined) {
      return Promise.resolve();
    }
    if (this.enabledEvents !== undefined && !this.enabledEvents.has(event.event)) {
      return Promise.resolve();
    }
    return this.publisher.publish(event);
  }
}

export class ZLinkLocationRuntimeMonitoringSource {
  private previousStatus?: string;
  private previousTopology?: string;
  private previousServiceSummary?: string;
  private storeFailure = false;

  constructor(
    private readonly registration: ZLinkPollingMonitoringRegistration,
    private readonly query: ZLinkLocationRuntimeQuery,
    private readonly publisher: ZLinkRuntimeEventPublisherContract
  ) {
    validateSourceName(registration.sourceName);
    validatePollingInterval('Location runtime', registration.intervalMs);
  }

  async pollOnce(signal?: AbortSignal): Promise<void> {
    let status;
    let topology;
    let serviceSummary;
    try {
      [status, topology, serviceSummary] = await Promise.all([
        this.query.getStatus(signal),
        this.query.listTopology({}, undefined, signal),
        this.query.listServiceSummaries({}, signal)
      ]);
    } catch (error) {
      if (!this.storeFailure) {
        this.storeFailure = true;
        await this.publisher.publish({
          sourceName: this.registration.sourceName,
          timestamp: new Date(),
          event: LocationRuntimeEventKind.StoreFailure
        } satisfies ZLinkLocationRuntimeEvent);
      }
      return;
    }

    if (this.storeFailure) {
      this.storeFailure = false;
      await this.publisher.publish({
        sourceName: this.registration.sourceName,
        timestamp: new Date(),
        event: LocationRuntimeEventKind.StoreRecovered
      } satisfies ZLinkLocationRuntimeEvent);
    }

    this.previousStatus = await publishLocationRuntimeIfChanged(
      this.publisher,
      this.registration.sourceName,
      LocationRuntimeEventKind.StatusChanged,
      this.previousStatus,
      status
    );
    this.previousTopology = await publishLocationRuntimeIfChanged(
      this.publisher,
      this.registration.sourceName,
      LocationRuntimeEventKind.TopologyChanged,
      this.previousTopology,
      topology.items
    );
    this.previousServiceSummary = await publishLocationRuntimeIfChanged(
      this.publisher,
      this.registration.sourceName,
      LocationRuntimeEventKind.ServiceSummaryChanged,
      this.previousServiceSummary,
      serviceSummary
    );
  }
}

export class ZLinkLocationMonitoringEventEmitter {
  static readonly disabled = new ZLinkLocationMonitoringEventEmitter({}, undefined);

  constructor(
    private readonly registration: {
      readonly peer?: ZLinkLocationMonitoringRegistration;
      readonly spot?: ZLinkLocationMonitoringRegistration;
      readonly actor?: ZLinkLocationMonitoringRegistration;
      readonly route?: ZLinkLocationMonitoringRegistration;
    },
    private readonly publisher?: ZLinkRuntimeEventPublisherContract
  ) {
    for (const source of [registration.peer, registration.spot, registration.actor, registration.route]) {
      if (source !== undefined) {
        validateSourceName(source.sourceName);
      }
    }
  }

  peerRowUpdated(key: ZLinkLocationKey, peer: ZLinkPeerLocation): void {
    this.publishPeer(PeerLocationEventKind.RowUpdated, { key, peer });
  }

  peerRowRemoved(key: ZLinkLocationKey): void {
    this.publishPeer(PeerLocationEventKind.RowRemoved, { key });
  }

  desiredSetChanged(change: ZLinkAutoConnectDesiredSetChange): void {
    this.publishPeer(PeerLocationEventKind.DesiredSetChanged, { desiredSetChange: change });
  }

  spotRowUpdated(key: ZLinkSpotLocationKey, spot: ZLinkSpotLocation): void {
    this.publishSpotLocation(SpotLocationEventKind.RowUpdated, { key, spot });
  }

  spotRowRemoved(key: ZLinkSpotLocationKey): void {
    this.publishSpotLocation(SpotLocationEventKind.RowRemoved, { key });
  }

  spotResolveMiss(key: ZLinkSpotLocationKey): void {
    this.publishSpotLocation(SpotLocationEventKind.ResolveMiss, { key });
  }

  actorRowUpdated(key: ZLinkActorLocationKey, actor: ZLinkActorLocation): void {
    this.publishActor(ActorLocationEventKind.RowUpdated, { key, actor });
  }

  actorRowRemoved(key: ZLinkActorLocationKey): void {
    this.publishActor(ActorLocationEventKind.RowRemoved, { key });
  }

  actorResolveMiss(key: ZLinkActorLocationKey): void {
    this.publishActor(ActorLocationEventKind.ResolveMiss, { key });
  }

  routeRowUpdated(key: ZLinkRouteLocationKey, route: ZLinkRouteLocation): void {
    this.publishRoute(RouteLocationEventKind.RowUpdated, { key, route });
  }

  routeRowRemoved(key: ZLinkRouteLocationKey): void {
    this.publishRoute(RouteLocationEventKind.RowRemoved, { key });
  }

  routeResolveMiss(key: ZLinkRouteLocationKey): void {
    this.publishRoute(RouteLocationEventKind.ResolveMiss, { key });
  }

  private publishPeer(event: ZLinkLocationPeerEvent['event'], payload: Record<string, unknown>): void {
    const registration = this.registration.peer;
    if (registration === undefined) {
      return;
    }
    this.publish({ sourceName: registration.sourceName, timestamp: new Date(), event, ...payload } as ZLinkLocationPeerEvent);
  }

  private publishSpotLocation(event: ZLinkLocationSpotEvent['event'], payload: Record<string, unknown>): void {
    const registration = this.registration.spot;
    if (registration === undefined) {
      return;
    }
    this.publish({ sourceName: registration.sourceName, timestamp: new Date(), event, ...payload } as ZLinkLocationSpotEvent);
  }

  private publishActor(event: ZLinkLocationActorEvent['event'], payload: Record<string, unknown>): void {
    const registration = this.registration.actor;
    if (registration === undefined) {
      return;
    }
    this.publish({ sourceName: registration.sourceName, timestamp: new Date(), event, ...payload } as ZLinkLocationActorEvent);
  }

  private publishRoute(event: ZLinkLocationRouteEvent['event'], payload: Record<string, unknown>): void {
    const registration = this.registration.route;
    if (registration === undefined) {
      return;
    }
    this.publish({ sourceName: registration.sourceName, timestamp: new Date(), event, ...payload } as ZLinkLocationRouteEvent);
  }

  private publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): void {
    if (this.publisher === undefined) {
      return;
    }
    void this.publisher.publish(event).catch((error) => {
      console.error('[location-monitoring-event-dispatch]', error);
    });
  }
}

export class ZLinkMeshMonitoringSource {
  private previousStatus?: string;
  private previousPeers?: string;

  constructor(
    private readonly registration: ZLinkPollingMonitoringRegistration,
    private readonly meshNode: ZLinkBackendMeshNode,
    private readonly publisher: ZLinkRuntimeEventPublisherContract,
    private readonly meshOptions?: ZLinkSpotNodeOptions
  ) {
    validateSourceName(registration.sourceName);
    validatePollingInterval('Mesh', registration.intervalMs);
  }

  async pollOnce(): Promise<void> {
    const status = this.meshNode.status();
    const backendPeers = this.meshNode.peers();
    const channels = backendPeers.map((peer) => peer.routingId === null
      ? { peerRid: '', generation: peer.lifecycleGeneration, names: [], weights: [] }
      : {
          peerRid: String(peer.routingId),
          generation: peer.lifecycleGeneration,
          ...this.meshNode.peerChannels(peer.routingId, peer.lifecycleGeneration)
        });
    const peers = backendPeers.map((peer, index): ZLinkMeshPeerSnapshot => ({
      rid: peer.routingId === null ? '' : String(peer.routingId),
      lifecycleGeneration: peer.lifecycleGeneration,
      descriptorRevision: peer.descriptorRevision,
      endpoint: peer.endpoint,
      admissionState: meshPeerStateName(peer.state),
      ready: peer.state === 3 && peer.routingId !== null,
      drainState: peer.state === 4 ? 'draining' : 'active',
      channelNames: [...(channels[index]?.names ?? [])],
      lastFailure: peer.lastError === 0 ? undefined : String(peer.lastError)
    }));
    const localChannels = Object.entries(this.meshOptions?.meshChannels ?? {}).map(([channelName, channel]) => {
      const readyMemberCount = BigInt(channels.filter((entry, index) =>
        peers[index]?.ready === true
        && entry.names.some((name, channelIndex) =>
          name === channelName && (entry.weights[channelIndex] ?? 0) > 0)
      ).length);
      const localWeight = channel.weight ?? 1;
      return {
        channelName,
        localWeight,
        readyMemberCount,
        selectable: localWeight > 0 || readyMemberCount > 0n
      };
    });
    const snapshot: ZLinkMeshNodeSnapshot = {
      meshName: status.meshName,
      rid: String(status.routingId),
      lifecycleGeneration: status.lifecycleGeneration,
      descriptorRevision: status.descriptorRevision,
      endpoint: status.localEndpoint,
      objectRole: this.meshOptions?.objectRole === 'server'
        ? ZLinkObjectRole.Server
        : this.meshOptions?.objectRole === 'client'
          ? ZLinkObjectRole.Client
          : ZLinkObjectRole.None,
      placementWeight: this.meshOptions?.placementWeight ?? 0,
      populationCapacity: {
        actors: {
          active: 0,
          reserved: 0,
          limit: this.meshOptions?.actorLimit ?? 0
        },
        spots: {
          active: 0,
          reserved: 0,
          limit: this.meshOptions?.spotLimit ?? 0
        },
        spotTypes: []
      },
      activationConcurrency: {
        active: 0,
        limit: this.meshOptions?.activationConcurrencyLimit ?? 128
      },
      applicationVersion: 0n,
      placementReservationFailureCount: 0n,
      objectCapabilities: [],
      state: meshNodeState(status.state),
      sequence: status.lastChangedMs,
      observedAt: new Date(),
      descriptorSources: [],
      peers,
      channels: localChannels,
      instanceSpots: [],
      claims: {
        applicationActive: status.pendingApplicationMessages > 0n,
        pendingApplicationWork: status.pendingApplicationMessages,
        infrastructureActive: status.pendingInfrastructureMessages > 0n,
        pendingInfrastructureWork: status.pendingInfrastructureMessages
      },
      location: { state: 'unknown' },
      drain: {
        state: meshNodeState(status.state),
        workSealed: status.state >= 5,
        pendingRequestCount: 0n,
        pendingTransferCount: 0n,
        pendingStreamBarrierCount: 0n
      }
    };

    this.previousStatus = await publishMeshStatusIfChanged(
      this.publisher,
      this.registration.sourceName,
      this.previousStatus,
      snapshot
    );
    this.previousPeers = await publishMeshPeersIfChanged(
      this.publisher,
      this.registration.sourceName,
      this.previousPeers,
      snapshot.peers
    );
  }
}

function meshPeerStateName(state: number): string {
  switch (state) {
    case 1: return 'configured';
    case 2: return 'connecting';
    case 3: return 'ready';
    case 4: return 'draining';
    default: return 'closed';
  }
}

function meshNodeState(state: number): ZLinkMeshNodeState {
  switch (state) {
    case 1: return ZLinkMeshNodeState.Starting;
    case 2:
    case 3:
    case 4: return ZLinkMeshNodeState.Serving;
    case 5: return ZLinkMeshNodeState.Draining;
    case 6: return ZLinkMeshNodeState.Stopped;
    default: return ZLinkMeshNodeState.Faulted;
  }
}

export * from './topology-runtime-projections';

function toSocketEvent(sourceName: string, raw: ZLinkBackendSocketMonitorEvent): ZLinkSocketEvent | undefined {
  const event = mapSocketEvent(raw.nativeEvent as ZLinkSocketNativeEventType, raw.value);
  if (event === undefined) {
    return undefined;
  }
  return {
    sourceName,
    timestamp: new Date(),
    event,
    routingId: raw.routingId === undefined ? undefined : normalizeOpaqueRoutingId(raw.routingId),
    localAddr: raw.localAddr,
    remoteAddr: raw.remoteAddr,
    diagnostic: {
      nativeEvent: raw.nativeEvent as ZLinkSocketNativeEventType,
      nativeValue: raw.value
    }
  };
}

function mapSocketEvent(
  nativeEvent: ZLinkSocketNativeEventType,
  nativeValue: number
): ZLinkSocketEventKind | undefined {
  switch (nativeEvent) {
    case SocketNativeEventType.Connected:
    case SocketNativeEventType.Accepted:
    case SocketNativeEventType.Listening:
      return SocketEventKind.Connected;
    case SocketNativeEventType.ConnectionReady:
      return SocketEventKind.ConnectionReady;
    case SocketNativeEventType.Disconnected:
      return nativeValue === ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED
        ? SocketEventKind.HandshakeFailed
        : SocketEventKind.Disconnected;
    case SocketNativeEventType.HandshakeFailedNoDetail:
    case SocketNativeEventType.HandshakeFailedProtocol:
    case SocketNativeEventType.HandshakeFailedAuth:
      return SocketEventKind.HandshakeFailed;
    case SocketNativeEventType.PeerAdmissionChanged:
      return SocketEventKind.PeerAdmissionChanged;
    case SocketNativeEventType.Closed:
    case SocketNativeEventType.MonitorStopped:
      return SocketEventKind.Closed;
    default:
      return undefined;
  }
}

async function publishMeshStatusIfChanged(
  publisher: ZLinkRuntimeEventPublisherContract,
  sourceName: string,
  previous: string | undefined,
  snapshot: ZLinkMeshNodeSnapshot
): Promise<string> {
  const current = stableSnapshot(snapshot);
  if (current === previous) {
    return previous;
  }
  await publisher.publish<ZLinkSpotEvent>({
    sourceName,
    timestamp: new Date(),
    event: SpotEventKind.StatusChanged,
    status: snapshot
  });
  return current;
}

async function publishMeshPeersIfChanged(
  publisher: ZLinkRuntimeEventPublisherContract,
  sourceName: string,
  previous: string | undefined,
  peers: readonly ZLinkMeshPeerSnapshot[]
): Promise<string> {
  const current = stableSnapshot(peers);
  if (current === previous) {
    return previous;
  }
  await publisher.publish<ZLinkSpotEvent>({
    sourceName,
    timestamp: new Date(),
    event: SpotEventKind.PeersChanged,
    peers
  });
  return current;
}

async function publishLocationRuntimeIfChanged<T>(
  publisher: ZLinkRuntimeEventPublisherContract,
  sourceName: string,
  event: ZLinkLocationRuntimeEventKind,
  previous: string | undefined,
  snapshot: T
): Promise<string> {
  const current = stableSnapshot(snapshot);
  if (current === previous) {
    return previous;
  }
  const base = { sourceName, timestamp: new Date() };
  const runtimeEvent: ZLinkLocationRuntimeEvent = event === LocationRuntimeEventKind.StatusChanged
    ? { ...base, event, status: snapshot as ZLinkLocationRuntimeStatus }
    : event === LocationRuntimeEventKind.TopologyChanged
      ? { ...base, event, topology: snapshot as readonly ZLinkLocationTopologyEntry[] }
      : { ...base, event: LocationRuntimeEventKind.ServiceSummaryChanged, serviceSummary: snapshot as readonly ZLinkLocationServiceSummary[] };
  await publisher.publish(runtimeEvent);
  return current;
}

function stableSnapshot(value: unknown): string {
  return JSON.stringify(value, (_key, item) => typeof item === 'bigint' ? item.toString() : item);
}

function validateSourceName(sourceName: string): void {
  if (sourceName.trim().length === 0) {
    throw new Error('Monitoring sourceName must not be empty.');
  }
}

function validatePollingInterval(sourceKind: string, intervalMs: number): void {
  if (!Number.isFinite(intervalMs) || intervalMs <= 0) {
    throw new Error(`${sourceKind} monitoring intervalMs must be greater than zero.`);
  }
}

export * from './message-flow';
export * from './flow-context';
export * from './runtime-metrics';
