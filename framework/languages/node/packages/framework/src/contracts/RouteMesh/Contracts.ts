import type { ActorRef, RoutingId } from '../Common';
import type {
  ZLinkObjectCapability,
  ZLinkObjectRole,
  ZLinkPopulationCapacity,
  ZLinkSpotTypeCapacity
} from '../Locations';

export interface ZLinkActorMembership {
  readonly actor: ActorRef;
  readonly actorType: string;
  readonly membershipEpoch: bigint;
}

export interface ZLinkLocationOptionValues {
  readonly heartbeatIntervalMs: number;
  readonly ownerLeaseTtlMs: number;
  readonly pollingIntervalMs: number;
  readonly storeFailureGraceMs: number;
  readonly routingIdFencingMarginMs: number;
  readonly ownerLeaseRenewTimeoutMs: number;
  readonly routeCacheMaxAgeMs: number;
  readonly messageFollowDurationMs: number;
  readonly maxActiveOutboundRelocations: number;
  readonly maxActiveInboundRelocations: number;
  readonly maxConcurrentRelocationCaptures: number;
  readonly maxConcurrentRelocationRestores: number;
  readonly maxRelocationPayloadInFlightBytes: number;
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

export interface ZLinkRouteMeshRuntimeOptions {
  mesh(meshName: string): ZLinkMeshPlacementRuntimeOptions;
  channel(channelName: string): ZLinkMeshChannelRuntimeOptions;
}

export interface ZLinkMeshPlacementRuntimeOptions {
  placementWeight: number;
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
  Starting = 0,
  Serving = 1,
  Draining = 2,
  Drained = 3,
  ForceStopping = 4,
  Stopped = 5,
  Faulted = 6
}

export interface ZLinkMeshChannelSnapshot {
  readonly channelName: string;
  readonly localWeight: number;
  readonly readyMemberCount: bigint;
  readonly selectable: boolean;
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

export interface ZLinkMeshNodeSnapshot {
  readonly meshName: string;
  readonly rid: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly objectRole: ZLinkObjectRole;
  readonly placementWeight: number;
  readonly populationCapacity: {
    readonly actors: ZLinkPopulationCapacity;
    readonly spots: ZLinkPopulationCapacity;
    readonly spotTypes: readonly ZLinkSpotTypeCapacity[];
  };
  readonly activationConcurrency: {
    readonly active: number;
    readonly limit: number;
  };
  readonly applicationVersion: bigint;
  readonly placementReservationFailureCount: bigint;
  readonly lastPlacementReservationFailure?: string;
  readonly objectCapabilities: readonly ZLinkObjectCapability[];
  readonly state: ZLinkMeshNodeState;
  readonly sequence: bigint;
  readonly observedAt: Date;
  readonly descriptorSources: readonly string[];
  readonly peers: readonly ZLinkMeshPeerSnapshot[];
  readonly channels: readonly ZLinkMeshChannelSnapshot[];
  readonly instanceSpots: readonly import('./RuntimeTopology').ZLinkInstanceSpotTypeSnapshot[];
  readonly claims: ZLinkMeshClaimSnapshot;
  readonly location: ZLinkLocationRuntimeSnapshot;
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
  readonly placementOutcome?: string;
  readonly capacity?: {
    readonly actorSlots: number;
    readonly spotSlots: number;
    readonly spotTypes: readonly {
      readonly objectKind: 'user_spot' | 'instance_spot';
      readonly stableType: string;
      readonly slots: number;
    }[];
  };
  readonly populationCapacity?: ZLinkMeshNodeSnapshot['populationCapacity'];
  readonly activationConcurrency?: ZLinkMeshNodeSnapshot['activationConcurrency'];
  readonly reason?: string;
  readonly state?: ZLinkMeshNodeState;
}

export interface ZLinkRouteMeshRuntime {
  snapshot(meshName: string): ZLinkMeshNodeSnapshot;
  observe(meshName: string, capacity?: number, signal?: AbortSignal): AsyncIterable<ZLinkMeshRuntimeEvent>;
  isReady(meshName: string): boolean;
}
