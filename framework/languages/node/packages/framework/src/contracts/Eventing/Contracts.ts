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

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
  register<TEvent extends ZLinkRuntimeEvent>(handler: ZLinkRuntimeEventHandler<TEvent>): void;
  publish<TEvent extends ZLinkRuntimeEvent>(event: TEvent): Promise<void>;
}

export enum ZLinkSocketEventKind {
  Connected = 0,
  ConnectionReady = 1,
  Disconnected = 2,
  HandshakeFailed = 3,
  PeerAdmissionChanged = 4,
  Closed = 5,
  Internal = 6
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

export interface ZLinkLocationRuntimeEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkLocationRuntimeEventKind;
  readonly status?: ZLinkLocationRuntimeStatus;
  readonly topology?: readonly ZLinkLocationTopologyEntry[];
  readonly topologyFilter?: ZLinkLocationTopologyFilter;
  readonly serviceSummary?: readonly ZLinkLocationServiceSummary[];
  readonly serviceSummaryFilter?: ZLinkLocationServiceSummaryFilter;
}

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

export interface ZLinkLocationPeerEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkLocationPeerEventKind;
  readonly key?: string;
  readonly peer?: ZLinkPeerLocation;
  readonly desiredSetChange?: ZLinkAutoConnectDesiredSetChange;
}

export enum ZLinkLocationSpotEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  ResolveMiss = 2
}

export interface ZLinkLocationSpotEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkLocationSpotEventKind;
  readonly key: ZLinkSpotLocationKey;
  readonly spot?: ZLinkSpotLocation;
}

export enum ZLinkLocationActorEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  ResolveMiss = 2
}

export interface ZLinkLocationActorEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkLocationActorEventKind;
  readonly key: ZLinkActorLocationKey;
  readonly actor?: ZLinkActorLocation;
}

export enum ZLinkLocationRouteEventKind {
  RowUpdated = 0,
  RowRemoved = 1,
  ResolveMiss = 2
}

export interface ZLinkLocationRouteEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkLocationRouteEventKind;
  readonly key: ZLinkRouteLocationKey;
  readonly route?: ZLinkRouteLocation;
}

export enum ZLinkSpotEventKind {
  StatusChanged = 0,
  PeersChanged = 1,
  SubjectsChanged = 2,
  TimerHandlerFailed = 3,
  TimerStoppedAfterUnhandledException = 4
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

export interface ZLinkSpotEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSpotEventKind;
  readonly status?: ZLinkSpotNodeStatus;
  readonly peers?: readonly ZLinkSpotNodePeerEntry[];
  readonly subjects?: readonly ZLinkSpotNodeSubjectEntry[];
  readonly timerDiagnostic?: ZLinkSpotTimerDiagnostic;
}
