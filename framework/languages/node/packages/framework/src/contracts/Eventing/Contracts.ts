import type { RoutingId } from '../Common';
import type {
  ZLinkActorLocation,
  ZLinkActorLocationKey,
  ZLinkLocationAutoConnectType,
  ZLinkLocationRuntimeStatus,
  ZLinkLocationServiceSummary,
  ZLinkLocationServiceSummaryFilter,
  ZLinkLocationTopologyEntry,
  ZLinkLocationTopologyFilter,
  ZLinkPeerLocation,
  ZLinkRouteLocation,
  ZLinkRouteLocationKey,
  ZLinkSpotLocation,
  ZLinkSpotLocationKey
} from '../Locations';
import type {
  ZLinkSpotNodePeerEntry,
  ZLinkSpotNodeStatus,
  ZLinkSpotNodeSubjectEntry
} from '../Spots';

export interface ZLinkMonitoringOptions {
  socket?: ZLinkSocketMonitoringRegistration[];
  spot?: ZLinkPollingMonitoringRegistration[];
  locationRuntime?: ZLinkPollingMonitoringRegistration[];
  locationPeer?: ZLinkLocationMonitoringRegistration[];
  locationSpot?: ZLinkLocationMonitoringRegistration[];
  locationActor?: ZLinkLocationMonitoringRegistration[];
  locationRoute?: ZLinkLocationMonitoringRegistration[];
}

export interface ZLinkSocketMonitoringRegistration {
  readonly sourceName: string;
  readonly events?: readonly ZLinkSocketEventKind[];
}

export interface ZLinkPollingMonitoringRegistration {
  readonly sourceName: string;
  readonly intervalMs: number;
}

export interface ZLinkLocationMonitoringRegistration {
  readonly sourceName: string;
}

export interface ZLinkRuntimeEvent {
  readonly sourceName: string;
  readonly timestamp: Date;
}

export type ZLinkFlowOrigin = 'Inbound' | 'Timer' | 'Application' | 'Lifecycle';

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
  register<TEvent extends ZLinkRuntimeEvent>(handler: ZLinkRuntimeEventHandler<TEvent>): void;
  publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void>;
}

export enum ZLinkSocketEventKind {
  Connected = 'connected',
  ConnectionReady = 'connectionReady',
  Disconnected = 'disconnected',
  HandshakeFailed = 'handshakeFailed',
  PeerAdmissionChanged = 'peerAdmissionChanged',
  Closed = 'closed',
  Internal = 'internal'
}

export enum ZLinkSocketNativeEventType {
  Connected = 0x0001,
  ConnectDelayed = 0x0002,
  ConnectRetried = 0x0004,
  Listening = 0x0008,
  BindFailed = 0x0010,
  Accepted = 0x0020,
  AcceptFailed = 0x0040,
  Closed = 0x0080,
  CloseFailed = 0x0100,
  Disconnected = 0x0200,
  MonitorStopped = 0x0400,
  HandshakeFailedNoDetail = 0x0800,
  ConnectionReady = 0x1000,
  HandshakeFailedProtocol = 0x2000,
  HandshakeFailedAuth = 0x4000,
  PeerAdmissionChanged = 0x8000
}

export interface ZLinkSocketDiagnostic {
  readonly nativeEvent: ZLinkSocketNativeEventType;
  readonly nativeValue: number;
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSocketEventKind;
  readonly routingId?: RoutingId;
  readonly localAddr: string;
  readonly remoteAddr: string;
  readonly diagnostic?: ZLinkSocketDiagnostic;
}

export enum ZLinkLocationRuntimeEventKind {
  StatusChanged = 0,
  TopologyChanged = 1,
  ServiceSummaryChanged = 2,
  StoreUnavailable = 3,
  StoreRecovered = 4
}

export type ZLinkLocationRuntimeEvent =
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.StatusChanged; readonly status: ZLinkLocationRuntimeStatus })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.TopologyChanged; readonly topology: readonly ZLinkLocationTopologyEntry[]; readonly topologyFilter?: ZLinkLocationTopologyFilter })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.ServiceSummaryChanged; readonly serviceSummary: readonly ZLinkLocationServiceSummary[]; readonly serviceSummaryFilter?: ZLinkLocationServiceSummaryFilter })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.StoreUnavailable | ZLinkLocationRuntimeEventKind.StoreRecovered });

export enum ZLinkLocationPeerEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  DesiredSetChanged = 2
}

export interface ZLinkAutoConnectDesiredSetChange {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly connectedEndpoints: readonly string[];
  readonly disconnectedEndpoints: readonly string[];
}

export type ZLinkLocationPeerEvent =
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationPeerEventKind.RowUpdated; readonly key: string; readonly peer: ZLinkPeerLocation })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationPeerEventKind.RowRemoved; readonly key: string })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationPeerEventKind.DesiredSetChanged; readonly desiredSetChange: ZLinkAutoConnectDesiredSetChange });

export enum ZLinkLocationSpotEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  ResolveMiss = 2
}

export type ZLinkLocationSpotEvent = ZLinkRuntimeEvent & (
  | { readonly event: ZLinkLocationSpotEventKind.RowUpdated; readonly key: ZLinkSpotLocationKey; readonly spot: ZLinkSpotLocation }
  | { readonly event: ZLinkLocationSpotEventKind.RowRemoved | ZLinkLocationSpotEventKind.ResolveMiss; readonly key: ZLinkSpotLocationKey }
);

export enum ZLinkLocationActorEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  ResolveMiss = 2
}

export type ZLinkLocationActorEvent = ZLinkRuntimeEvent & (
  | { readonly event: ZLinkLocationActorEventKind.RowUpdated; readonly key: ZLinkActorLocationKey; readonly actor: ZLinkActorLocation }
  | { readonly event: ZLinkLocationActorEventKind.RowRemoved | ZLinkLocationActorEventKind.ResolveMiss; readonly key: ZLinkActorLocationKey }
);

export enum ZLinkLocationRouteEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  ResolveMiss = 2
}

export type ZLinkLocationRouteEvent = ZLinkRuntimeEvent & (
  | { readonly event: ZLinkLocationRouteEventKind.RowUpdated; readonly key: ZLinkRouteLocationKey; readonly route: ZLinkRouteLocation }
  | { readonly event: ZLinkLocationRouteEventKind.RowRemoved | ZLinkLocationRouteEventKind.ResolveMiss; readonly key: ZLinkRouteLocationKey }
);

export enum ZLinkSpotEventKind {
  StatusChanged = 'statusChanged',
  PeersChanged = 'peersChanged',
  SubjectsChanged = 'subjectsChanged',
  TimerHandlerFailed = 'timerHandlerFailed',
  TimerStoppedAfterUnhandledException = 'timerStoppedAfterUnhandledException'
}

export interface ZLinkSpotTimerDiagnostic {
  readonly spotRid: RoutingId;
  readonly isEntrySpot: boolean;
  readonly timerName: string;
  readonly handlerType: string;
  readonly deliveryIndex: bigint;
  readonly scheduledIndex: bigint;
  readonly exceptionType: string;
  readonly exceptionMessage: string;
}

export type ZLinkSpotEvent =
  | (ZLinkRuntimeEvent & { readonly event: ZLinkSpotEventKind.StatusChanged; readonly status: ZLinkSpotNodeStatus })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkSpotEventKind.PeersChanged; readonly peers: readonly ZLinkSpotNodePeerEntry[] })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkSpotEventKind.SubjectsChanged; readonly subjects: readonly ZLinkSpotNodeSubjectEntry[] })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkSpotEventKind.TimerHandlerFailed | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException; readonly timerDiagnostic: ZLinkSpotTimerDiagnostic });
