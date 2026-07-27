import type { RoutingId, SpotId } from '../Common';
import type {
  ZLinkLocationRuntimeStatus,
  ZLinkLocationServiceSummary,
  ZLinkLocationTopologyEntry,
} from '../Locations';

export interface ZLinkMonitoringOptions {
  socket?: ZLinkSocketMonitoringRegistration[];
  locationRuntime?: ZLinkPollingMonitoringRegistration[];
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

export type ZLinkFlowOrigin = 'Inbound' | 'Timer' | 'Application' | 'Lifecycle';

export interface ZLinkRuntimeEventHandler<TEvent extends ZLinkRuntimeEvent> {
  handle(event: TEvent): Promise<void>;
}

export enum ZLinkSocketEventKind {
  Connected = 'connected',
  ConnectionReady = 'connectionReady',
  Disconnected = 'disconnected',
  HandshakeFailed = 'handshakeFailed',
  PeerAdmissionChanged = 'peerAdmissionChanged',
  Closed = 'closed'
}

export interface ZLinkSocketEvent extends ZLinkRuntimeEvent {
  readonly event: ZLinkSocketEventKind;
  readonly routingId?: RoutingId;
  readonly localAddr: string;
  readonly remoteAddr: string;
}

export enum ZLinkLocationRuntimeEventKind {
  StatusChanged = 0,
  TopologyChanged = 1,
  ServiceSummaryChanged = 2,
  StoreFailure = 3,
  StoreRecovered = 4
}

export type ZLinkLocationRuntimeEvent =
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.StatusChanged; readonly status: ZLinkLocationRuntimeStatus })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.TopologyChanged; readonly topology: readonly ZLinkLocationTopologyEntry[] })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.ServiceSummaryChanged; readonly serviceSummary: readonly ZLinkLocationServiceSummary[] })
  | (ZLinkRuntimeEvent & { readonly event: ZLinkLocationRuntimeEventKind.StoreFailure | ZLinkLocationRuntimeEventKind.StoreRecovered });

export enum ZLinkSpotEventKind {
  TimerHandlerFailed = 'timerHandlerFailed',
  TimerStoppedAfterUnhandledException = 'timerStoppedAfterUnhandledException'
}

export interface ZLinkSpotTimerDiagnostic {
  readonly spotId: SpotId;
  readonly isEntrySpot: boolean;
  readonly timerName: string;
  readonly handlerType: string;
  readonly deliveryIndex: bigint;
  readonly scheduledIndex: bigint;
  readonly exceptionType: string;
  readonly exceptionMessage: string;
}

export type ZLinkSpotEvent =
  ZLinkRuntimeEvent & {
    readonly event:
      | ZLinkSpotEventKind.TimerHandlerFailed
      | ZLinkSpotEventKind.TimerStoppedAfterUnhandledException;
    readonly timerDiagnostic: ZLinkSpotTimerDiagnostic;
  };
