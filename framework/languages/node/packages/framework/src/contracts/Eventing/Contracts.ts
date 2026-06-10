import type { RoutingId } from '../Common';
import type {
  ZLinkRegistryServiceSummaryEntry,
  ZLinkRegistryStatus,
  ZLinkRegistryTopologyEntry
} from '../Registry';
import type {
  ZLinkSpotNodePeerEntry,
  ZLinkSpotNodeStatus,
  ZLinkSpotNodeSubjectEntry
} from '../Spots';

export interface ZLinkMonitoringOptions {
  socket?: ZLinkSocketMonitoringRegistration[];
  registry?: ZLinkPollingMonitoringRegistration[];
  spot?: ZLinkPollingMonitoringRegistration[];
}

export interface ZLinkSocketMonitoringRegistration {
  readonly sourceName: string;
  readonly events?: readonly ZLinkSocketEventKind[];
}

export interface ZLinkPollingMonitoringRegistration {
  readonly sourceName: string;
  readonly intervalMs: number;
}

export interface ZLinkRuntimeEvent {
  readonly sourceName: string;
  readonly timestamp: Date;
}

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export interface ZLinkRuntimeEventPublisher {
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

export enum ZLinkRegistryEventKind {
  StatusChanged = 0,
  TopologyChanged = 1,
  ServiceSummaryChanged = 2
}

export interface ZLinkRegistryEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkRegistryEventKind;
  readonly status?: ZLinkRegistryStatus;
  readonly topology?: readonly ZLinkRegistryTopologyEntry[];
  readonly serviceSummary?: readonly ZLinkRegistryServiceSummaryEntry[];
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
