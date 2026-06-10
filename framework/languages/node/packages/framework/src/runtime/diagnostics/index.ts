import type {
  ZLinkPollingMonitoringRegistration,
  ZLinkRegistryEvent,
  ZLinkRegistryEventKind,
  ZLinkRegistryQuery,
  ZLinkRuntimeEvent,
  ZLinkRuntimeEventHandler,
  ZLinkRuntimeEventPublisher as ZLinkRuntimeEventPublisherContract,
  ZLinkSocketEvent,
  ZLinkSocketEventKind,
  ZLinkSocketMonitoringRegistration,
  ZLinkSocketNativeEventType,
  ZLinkSpotEvent,
  ZLinkSpotEventKind
} from '../../contracts';
import {
  ZLinkRegistryEventKind as RegistryEventKind,
  ZLinkSocketEventKind as SocketEventKind,
  ZLinkSocketNativeEventType as SocketNativeEventType,
  ZLinkSpotEventKind as SpotEventKind
} from '../../contracts';
import type {
  ZLinkBackendSpotNode,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSocketMonitorEvent
} from '../backend';

export class DefaultZLinkRuntimeEventPublisher implements ZLinkRuntimeEventPublisherContract {
  private readonly handlers: ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>[] = [];

  register<TEvent extends ZLinkRuntimeEvent>(handler: ZLinkRuntimeEventHandler<TEvent>): void {
    this.handlers.push(handler as ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>);
  }

  async publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void> {
    for (const handler of this.handlers) {
      await handler.handle(event);
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

export class ZLinkRegistryMonitoringSource {
  private previousStatus?: string;
  private previousTopology?: string;
  private previousServiceSummary?: string;

  constructor(
    private readonly registration: ZLinkPollingMonitoringRegistration,
    private readonly query: ZLinkRegistryQuery,
    private readonly publisher: ZLinkRuntimeEventPublisherContract
  ) {
    validateSourceName(registration.sourceName);
    validatePollingInterval('Registry', registration.intervalMs);
  }

  async pollOnce(signal?: AbortSignal): Promise<void> {
    const [status, topology, serviceSummary] = await Promise.all([
      this.query.status(signal),
      this.query.topology(undefined, signal),
      this.query.serviceSummary(undefined, signal)
    ]);

    this.previousStatus = await publishIfChanged(
      this.publisher,
      this.registration.sourceName,
      RegistryEventKind.StatusChanged,
      this.previousStatus,
      status
    );
    this.previousTopology = await publishIfChanged(
      this.publisher,
      this.registration.sourceName,
      RegistryEventKind.TopologyChanged,
      this.previousTopology,
      topology
    );
    this.previousServiceSummary = await publishIfChanged(
      this.publisher,
      this.registration.sourceName,
      RegistryEventKind.ServiceSummaryChanged,
      this.previousServiceSummary,
      serviceSummary
    );
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
    event: mapSocketEvent(raw.nativeEvent as ZLinkSocketNativeEventType),
    routingId: raw.routingId as unknown as string | undefined,
    localAddr: raw.localAddr,
    remoteAddr: raw.remoteAddr,
    diagnostic: {
      nativeEvent: raw.nativeEvent as ZLinkSocketNativeEventType,
      nativeValue: raw.value
    }
  };
}

function mapSocketEvent(nativeEvent: ZLinkSocketNativeEventType): ZLinkSocketEventKind {
  switch (nativeEvent) {
    case SocketNativeEventType.Connected:
    case SocketNativeEventType.Accepted:
    case SocketNativeEventType.Listening:
      return SocketEventKind.Connected;
    case SocketNativeEventType.ConnectionReady:
      return SocketEventKind.ConnectionReady;
    case SocketNativeEventType.Disconnected:
      return SocketEventKind.Disconnected;
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
  const runtimeEvent: ZLinkSpotEvent = {
    sourceName,
    timestamp: new Date(),
    event,
    ...(event === SpotEventKind.StatusChanged ? { status: snapshot as ZLinkSpotEvent['status'] } : {}),
    ...(event === SpotEventKind.PeersChanged ? { peers: snapshot as ZLinkSpotEvent['peers'] } : {}),
    ...(event === SpotEventKind.SubjectsChanged ? { subjects: snapshot as ZLinkSpotEvent['subjects'] } : {})
  };
  await publisher.publish(runtimeEvent);
  return current;
}

async function publishIfChanged<T>(
  publisher: ZLinkRuntimeEventPublisherContract,
  sourceName: string,
  event: ZLinkRegistryEventKind,
  previous: string | undefined,
  snapshot: T
): Promise<string> {
  const current = stableSnapshot(snapshot);
  if (current === previous) {
    return previous;
  }
  const runtimeEvent: ZLinkRegistryEvent = {
    sourceName,
    timestamp: new Date(),
    event,
    ...(event === RegistryEventKind.StatusChanged ? { status: snapshot as ZLinkRegistryEvent['status'] } : {}),
    ...(event === RegistryEventKind.TopologyChanged ? { topology: snapshot as ZLinkRegistryEvent['topology'] } : {}),
    ...(event === RegistryEventKind.ServiceSummaryChanged
      ? { serviceSummary: snapshot as ZLinkRegistryEvent['serviceSummary'] }
      : {})
  };
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
