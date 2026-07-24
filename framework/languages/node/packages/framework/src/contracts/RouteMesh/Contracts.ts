import type { ActorRef, RoutingId } from '../Common';

export interface ZLinkActorMembership {
  readonly actor: ActorRef;
  readonly actorType: string;
  readonly membershipEpoch: bigint;
}

export interface ZLinkActorJoinRequest {
  readonly actor: ActorRef;
  readonly actorType: string;
  readonly expectedMembershipEpoch: bigint;
}

export interface ZLinkLocationOptionValues {
  readonly heartbeatIntervalMs: number;
  readonly ownerLeaseTtlMs: number;
  readonly pollingIntervalMs: number;
  readonly storeFailureGraceMs: number;
  readonly routingIdFencingMarginMs: number;
  readonly ownerLeaseRenewTimeoutMs: number;
}

export type ZLinkMessageSurface =
  | 'node'
  | 'channel'
  | 'spot'
  | 'logical_multicast'
  | 'actor'
  | 'stream'
  | 'classic_fanout'
  | 'actor_transfer';

export type ZLinkMessageKind =
  | 'send'
  | 'request'
  | 'response'
  | 'error'
  | 'publish'
  | 'control';

export type ZLinkRequestFailureReason = 'timeout' | 'cancelled' | 'shutdown';

export class ZLinkRequestFailureError extends Error {
  readonly reason: ZLinkRequestFailureReason;

  constructor(reason: ZLinkRequestFailureReason, message: string, cause?: unknown) {
    super(message, { cause });
    this.name = 'ZLinkRequestFailureError';
    this.reason = reason;
  }
}

export interface ZLinkRuntimeErrorEvent {
  readonly eventId: 'zlink.runtime_error';
  readonly timestamp: Date;
  readonly kind: 'observer_failed';
  readonly source: 'message_flow_observer';
  readonly reason: string;
}

export interface ZLinkRuntimeErrorSink {
  onRuntimeError(error: ZLinkRuntimeErrorEvent): Promise<void> | void;
}

export enum ZLinkSubmitStatus {
  Submitted = 'submitted',
  Backpressured = 'backpressured',
  TimedOut = 'timedOut',
  TargetNotFound = 'targetNotFound',
  RouteNotConnected = 'routeNotConnected',
  Shutdown = 'shutdown'
}

export interface ZLinkSubmitResult {
  readonly status: ZLinkSubmitStatus;
}

export interface ZLinkLogicalMulticastDetail {
  readonly snapshotRemoteNodeCount: bigint;
  readonly admittedRemoteNodeCount: bigint;
  readonly droppedRemoteNodeCount: bigint;
  readonly unreachableRemoteNodeCount: bigint;
  readonly snapshotLocalSpotCount: bigint;
  readonly admittedLocalSpotCount: bigint;
  readonly droppedLocalSpotCount: bigint;
}

export interface ZLinkPublishResult {
  /**
   * Reports source-local outbound transport queue admission for remote targets
   * and origin-local Spot queue admission for local targets. It does not wait
   * for remote Spot queue admission, acknowledgements, or handler execution.
   */
  readonly status: ZLinkSubmitStatus;
  readonly detail: ZLinkLogicalMulticastDetail;
}

export interface ZLinkRouteMeshRuntimeOptions {
  meshNode(meshName: string): ZLinkMeshNodeRuntimeOptions;
  channel(meshName: string, channelName: string): ZLinkMeshChannelRuntimeOptions;
}

export interface ZLinkMeshNodeRuntimeOptions {
  maxMessageSize: number;
}

export interface ZLinkMeshChannelRuntimeOptions {
  weight: number;
}

export interface ZLinkMeshPeerSnapshot {
  readonly rid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly admissionState: string;
  readonly ready: boolean;
  readonly drainState: string;
  readonly channelNames: readonly string[];
  readonly lastFailure?: string;
}

export enum ZLinkMeshNodeState {
  Starting = 1,
  Serving = 2,
  Draining = 3,
  Drained = 4,
  ForceStopping = 5,
  Stopped = 6,
  Faulted = 7
}

export interface ZLinkMeshChannelSnapshot {
  readonly channelName: string;
  readonly localWeight: number;
  readonly readyMemberCount: bigint;
  readonly selectable: boolean;
}

export interface ZLinkLogicalMulticastSnapshot {
  readonly submitted: bigint;
  readonly backpressured: bigint;
  readonly dropped: bigint;
  readonly remoteSnapshotCount: bigint;
  readonly remoteAdmittedCount: bigint;
  readonly remoteDroppedCount: bigint;
  readonly localSnapshotCount: bigint;
  readonly localAdmittedCount: bigint;
  readonly localDroppedCount: bigint;
}

export interface ZLinkMeshClaimSnapshot {
  readonly applicationActive: boolean;
  readonly pendingApplicationWork: bigint;
  readonly infrastructureActive: boolean;
  readonly pendingInfrastructureWork: bigint;
}

export interface ZLinkLocationRuntimeSnapshot {
  readonly state: string;
  readonly lastSuccessAt?: Date;
  readonly lastFailureAt?: Date;
}

export interface ZLinkMeshDrainSnapshot {
  readonly state: ZLinkMeshNodeState;
  readonly deadline?: Date;
  readonly workSealed: boolean;
  readonly pendingRequestCount: bigint;
  readonly pendingTransferCount: bigint;
  readonly pendingStreamBarrierCount: bigint;
}

export interface ZLinkMeshNodeSnapshot {
  readonly meshName: string;
  readonly rid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly state: ZLinkMeshNodeState;
  readonly sequence: bigint;
  readonly observedAt: Date;
  readonly descriptorSources: readonly string[];
  readonly peers: readonly ZLinkMeshPeerSnapshot[];
  readonly channels: readonly ZLinkMeshChannelSnapshot[];
  readonly multicast: ZLinkLogicalMulticastSnapshot;
  readonly claims: ZLinkMeshClaimSnapshot;
  readonly location: ZLinkLocationRuntimeSnapshot;
  readonly drain: ZLinkMeshDrainSnapshot;
}

export interface ZLinkMeshRuntimeEvent {
  readonly identifier: string;
  readonly sequence: bigint;
  readonly timestamp: Date;
  readonly meshName: string;
  readonly sourceRid: RoutingId;
  readonly peerRid?: RoutingId;
  readonly lifecycleGeneration?: bigint;
  readonly descriptorRevision?: bigint;
  readonly channelName?: string;
  readonly claimDomain?: string;
  readonly messageKind?: string;
  readonly remoteSnapshotCount?: bigint;
  readonly remoteAdmittedCount?: bigint;
  readonly remoteDroppedCount?: bigint;
  readonly localSnapshotCount?: bigint;
  readonly localAdmittedCount?: bigint;
  readonly localDroppedCount?: bigint;
  readonly reason?: string;
  readonly state?: ZLinkMeshNodeState;
}

export type ZLinkDrainForceReason =
  | 'deadline_exceeded'
  | 'drain_state_publish_failed'
  | 'owner_cleanup_failed'
  | 'teardown_failed';

export type ZLinkMeshDrainResult =
  | { readonly kind: 'drained' }
  | { readonly kind: 'forceStopped'; readonly reason: ZLinkDrainForceReason };

export interface ZLinkRouteMeshRuntime {
  snapshot(meshName: string): ZLinkMeshNodeSnapshot;
  observe(meshName: string, capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkMeshRuntimeEvent>;
  isReady(meshName: string): boolean;
  drain(meshName: string, deadlineMs?: number, signal?: AbortSignal): Promise<ZLinkMeshDrainResult>;
  awaitDrained(meshName: string, signal?: AbortSignal): Promise<ZLinkMeshDrainResult>;
}
