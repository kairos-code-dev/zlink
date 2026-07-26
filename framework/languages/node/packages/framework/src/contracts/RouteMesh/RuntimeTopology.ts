import type { RoutingId } from '../Common';
import type { ZLinkFrameworkRuntimeState } from '../Locations';

export enum ZLinkTerminationIntent {
  Retire = 0,
  Shutdown = 1
}

export enum ZLinkTerminationOutcome {
  Stopped = 0,
  Blocked = 1,
  ForceStopped = 2
}

export enum ZLinkTerminationReason {
  None = 0,
  TargetUnavailable = 1,
  StoreUnavailable = 2,
  RelocationDisabled = 3,
  StateIncompatible = 4,
  DeadlineExceeded = 5,
  RelocationFailed = 6,
  TeardownFailed = 7,
  RuntimeNotReady = 8,
  ManualTopologyUnsupported = 9
}

export interface ZLinkTerminationResult {
  readonly effectiveIntent: ZLinkTerminationIntent;
  readonly outcome: ZLinkTerminationOutcome;
  readonly reason: ZLinkTerminationReason;
}

export interface ZLinkTerminationOptions {
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

export interface ZLinkFrameworkRuntimeSnapshot {
  readonly state: ZLinkFrameworkRuntimeState;
  readonly effectiveIntent?: ZLinkTerminationIntent;
  readonly deadline?: Date;
  readonly workSealed: boolean;
  readonly blockerReason?: ZLinkTerminationReason;
  readonly pendingRequestCount: bigint;
  readonly pendingRelocationCount: bigint;
  readonly pendingStreamBarrierCount: bigint;
  readonly terminalResult?: ZLinkTerminationResult;
  readonly sequence: bigint;
  readonly observedAt: Date;
}

export interface ZLinkFrameworkRuntimeEvent {
  readonly identifier: 'zlink.runtime.host.termination_changed';
  readonly sequence: bigint;
  readonly timestamp: Date;
  readonly state: ZLinkFrameworkRuntimeState;
  readonly effectiveIntent?: ZLinkTerminationIntent;
  readonly outcome?: ZLinkTerminationOutcome;
  readonly reason?: ZLinkTerminationReason;
}

export interface ZLinkFrameworkRuntime {
  readonly state: ZLinkFrameworkRuntimeState;
  readonly isReady: boolean;
  snapshot(): ZLinkFrameworkRuntimeSnapshot;
  observe(capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkFrameworkRuntimeEvent>;
  retire(options?: ZLinkTerminationOptions): Promise<ZLinkTerminationResult>;
  shutdown(options?: ZLinkTerminationOptions): Promise<ZLinkTerminationResult>;
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
