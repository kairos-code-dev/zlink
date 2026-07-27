import type { RoutingId } from '../Common';
import type { ZLinkFrameworkRuntimeState } from '../Locations';

export enum ZLinkFrameworkRelocationOutcome {
  Relocated = 0,
  Blocked = 1
}

export enum ZLinkFrameworkRelocationMode {
  PlannedMaintenance = 0,
  RollingUpdate = 1
}

export enum ZLinkFrameworkRelocationReason {
  None = 0,
  TargetUnavailable = 1,
  StoreUnavailable = 2,
  RelocationDisabled = 3,
  StateIncompatible = 4,
  DeadlineExceeded = 5,
  RelocationFailed = 6,
  RuntimeNotReady = 7,
  ManualTopologyUnsupported = 8,
  ShutdownRequested = 9,
  OperationInProgress = 10
}

export interface ZLinkFrameworkRelocationOptions {
  readonly mode: ZLinkFrameworkRelocationMode;
  readonly targetApplicationVersion?: bigint;
  readonly deadlineMs?: number;
  readonly signal?: AbortSignal;
}

export interface ZLinkFrameworkRelocationResult {
  readonly mode: ZLinkFrameworkRelocationMode;
  readonly effectiveTargetApplicationVersion: bigint;
  readonly outcome: ZLinkFrameworkRelocationOutcome;
  readonly reason: ZLinkFrameworkRelocationReason;
}

export enum ZLinkFrameworkTerminationOutcome {
  Stopped = 0,
  ForceStopped = 1
}

export enum ZLinkFrameworkTerminationReason {
  None = 0,
  DeadlineExceeded = 1,
  TeardownFailed = 2
}

export interface ZLinkFrameworkTerminationResult {
  readonly outcome: ZLinkFrameworkTerminationOutcome;
  readonly reason: ZLinkFrameworkTerminationReason;
}

export interface ZLinkFrameworkLifecycleOptions {
  readonly deadlineMs?: number;
  readonly signal?: AbortSignal;
}

export interface ZLinkInstanceSpotTypeSnapshot {
  readonly instanceSpotType: string;
  readonly activeCount: bigint;
  readonly activatingCount: bigint;
  readonly closingCount: bigint;
  readonly pendingMessageCount: bigint;
  readonly pendingByteCount: bigint;
  readonly lastActivationOutcome?: string;
}

export interface ZLinkFrameworkRuntimeStatus {
  readonly state: ZLinkFrameworkRuntimeState;
  readonly isReady: boolean;
  readonly acceptingWork: boolean;
  readonly deadline?: Date;
  readonly relocationResult?: ZLinkFrameworkRelocationResult;
  readonly terminationResult?: ZLinkFrameworkTerminationResult;
  readonly sequence: bigint;
  readonly observedAt: Date;
}

export interface ZLinkFrameworkRuntime {
  readonly status: ZLinkFrameworkRuntimeStatus;
  observe(signal?: AbortSignal): AsyncIterable<ZLinkFrameworkRuntimeStatus>;
  relocate(options: ZLinkFrameworkRelocationOptions): Promise<ZLinkFrameworkRelocationResult>;
  shutdown(options?: ZLinkFrameworkLifecycleOptions): Promise<ZLinkFrameworkTerminationResult>;
}

export type ZLinkClientServerRole = 'client' | 'server' | 'clientAndServer';
export type ZLinkClientServerServerState =
  | 'configured' | 'connecting' | 'ready' | 'draining' | 'disconnected' | 'rejected';

export interface ZLinkClientServerServerSnapshot {
  readonly serverRid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly weight: number;
  readonly ready: boolean;
  readonly state: ZLinkClientServerServerState;
  readonly descriptorSource: string;
  readonly lastFailure?: string;
}

export interface ZLinkClientServerChannelSnapshot {
  readonly channelName: string;
  readonly localRole: ZLinkClientServerRole;
  readonly selectable: boolean;
  readonly readyServerCount: number;
  readonly connectionIntentCount: number;
  readonly pendingRequestCount: number;
  readonly sequence: bigint;
  readonly observedAt: Date;
  readonly servers: readonly ZLinkClientServerServerSnapshot[];
  readonly location: import('./Contracts').ZLinkLocationRuntimeSnapshot;
}

export interface ZLinkClientServerRuntimeEvent {
  readonly identifier: string;
  readonly sequence: bigint;
  readonly timestamp: Date;
  readonly channelName: string;
  readonly serverRid?: RoutingId;
  readonly lifecycleGeneration?: bigint;
  readonly descriptorRevision?: bigint;
  readonly weight?: number;
  readonly ready?: boolean;
  readonly state?: ZLinkClientServerServerState;
  readonly reason?: string;
}

export interface ZLinkClientServerRuntime {
  snapshot(channelName: string): ZLinkClientServerChannelSnapshot;
  observe(
    channelName: string,
    capacity?: number,
    signal?: AbortSignal
  ): AsyncIterable<ZLinkClientServerRuntimeEvent>;
  isReady(channelName: string): boolean;
}

export type ZLinkFanoutPublisherConnectionState =
  | 'connecting' | 'ready' | 'disconnected' | 'reconnecting'
  | 'excluded_draining' | 'excluded_stale';

export interface ZLinkFanoutPublisherConnectionSnapshot {
  readonly publisherRid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly connectionIntent: boolean;
  readonly ready: boolean;
  readonly state: ZLinkFanoutPublisherConnectionState;
  readonly lastFailure?: string;
}

export interface ZLinkFanoutChannelSnapshot {
  readonly channelName: string;
  readonly connectionIntentCount: number;
  readonly readyConnectionCount: number;
  readonly sequence: bigint;
  readonly observedAt: Date;
  readonly publishers: readonly ZLinkFanoutPublisherConnectionSnapshot[];
  readonly location: import('./Contracts').ZLinkLocationRuntimeSnapshot;
}

export type ZLinkFanoutRuntimeEvent =
  | {
      readonly identifier: 'zlink.runtime.fanout.publisher_changed';
      readonly sequence: bigint;
      readonly timestamp: Date;
      readonly channelName: string;
      readonly entry: ZLinkFanoutPublisherConnectionSnapshot;
    }
  | {
      readonly identifier: 'zlink.runtime.location.store_changed';
      readonly sequence: bigint;
      readonly timestamp: Date;
      readonly channelName: string;
      readonly location: import('./Contracts').ZLinkLocationRuntimeSnapshot;
    };

export interface ZLinkFanoutRuntime {
  snapshot(channelName: string): ZLinkFanoutChannelSnapshot;
  observe(channelName: string, capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkFanoutRuntimeEvent>;
}
