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
  ZLinkSpotEvent,
  ZLinkSpotEventKind,
  ZLinkSpotNodeStatus,
  ZLinkSpotNodePeerEntry,
  ZLinkSpotNodeSubjectEntry
} from '../../contracts';
import {
  ZLinkLocationActorEventKind as ActorLocationEventKind,
  ZLinkLocationPeerEventKind as PeerLocationEventKind,
  ZLinkLocationRouteEventKind as RouteLocationEventKind,
  ZLinkLocationRuntimeEventKind as LocationRuntimeEventKind,
  ZLinkLocationSpotEventKind as SpotLocationEventKind,
  ZLinkSocketEventKind as SocketEventKind,
  ZLinkSocketNativeEventType as SocketNativeEventType,
  ZLinkSpotEventKind as SpotEventKind
} from '../../contracts';
import type {
  ZLinkBackendSpotNode,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSocketMonitorEvent
} from '../backend';
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

export class ZLinkSpotMonitoringSource {
  private previousStatus?: string;
  private previousPeers?: string;
  private previousSubjects?: string;

  constructor(
    private readonly registration: ZLinkPollingMonitoringRegistration,
    private readonly spotNode: ZLinkBackendSpotNode,
    private readonly publisher: ZLinkRuntimeEventPublisherContract
  ) {
    validateSourceName(registration.sourceName);
    validatePollingInterval('Spot', registration.intervalMs);
  }

  async pollOnce(): Promise<void> {
    const status = this.spotNode.status();
    const peers = this.spotNode.peers();
    const subjects = this.spotNode.subjects();

    this.previousStatus = await publishSpotIfChanged(
      this.publisher,
      this.registration.sourceName,
      SpotEventKind.StatusChanged,
      this.previousStatus,
      status
    );
    this.previousPeers = await publishSpotIfChanged(
      this.publisher,
      this.registration.sourceName,
      SpotEventKind.PeersChanged,
      this.previousPeers,
      peers
    );
    this.previousSubjects = await publishSpotIfChanged(
      this.publisher,
      this.registration.sourceName,
      SpotEventKind.SubjectsChanged,
      this.previousSubjects,
      subjects
    );
  }
}

function toSocketEvent(sourceName: string, raw: ZLinkBackendSocketMonitorEvent): ZLinkSocketEvent {
  return {
    sourceName,
    timestamp: new Date(),
    event: mapSocketEvent(raw.nativeEvent as ZLinkSocketNativeEventType, raw.value),
    routingId: raw.routingId === undefined ? undefined : normalizeOpaqueRoutingId(raw.routingId),
    localAddr: raw.localAddr,
    remoteAddr: raw.remoteAddr,
    diagnostic: {
      nativeEvent: raw.nativeEvent as ZLinkSocketNativeEventType,
      nativeValue: raw.value
    }
  };
}

function mapSocketEvent(nativeEvent: ZLinkSocketNativeEventType, nativeValue: number): ZLinkSocketEventKind {
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
      return SocketEventKind.Internal;
  }
}

async function publishSpotIfChanged<T>(
  publisher: ZLinkRuntimeEventPublisherContract,
  sourceName: string,
  event: ZLinkSpotEventKind,
  previous: string | undefined,
  snapshot: T
): Promise<string> {
  const current = stableSnapshot(snapshot);
  if (current === previous) {
    return previous;
  }
  const base = { sourceName, timestamp: new Date() };
  const runtimeEvent: ZLinkSpotEvent = event === SpotEventKind.StatusChanged
    ? { ...base, event, status: snapshot as ZLinkSpotNodeStatus }
    : event === SpotEventKind.PeersChanged
      ? { ...base, event, peers: snapshot as readonly ZLinkSpotNodePeerEntry[] }
      : { ...base, event: SpotEventKind.SubjectsChanged, subjects: snapshot as readonly ZLinkSpotNodeSubjectEntry[] };
  await publisher.publish(runtimeEvent);
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
