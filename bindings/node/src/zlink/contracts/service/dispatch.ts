// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Message, MessageLike } from '../messaging';
import type { SubmitResult } from '../errors';

/** A 128-bit mesh operation id whose completion arrives through pull dispatch. */
export interface MeshOperationId {
  readonly high: bigint;
  readonly low: bigint;
}

/** A 128-bit actor-transfer id (zlink_actor_transfer_id_t). */
export interface ActorTransferId {
  readonly high: bigint;
  readonly low: bigint;
}

/** A reference to an actor: its home node, id, and lifecycle generation. */
export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}

/** Where an actor currently resides within the mesh. */
export interface ActorLocation {
  readonly actor: ActorRef;
  readonly spotRid: RoutingId | null;
  readonly spotGeneration: bigint;
  readonly membershipEpoch: bigint;
}

/** Which ready-index domains a drain or ready handler processes. */
export const ReadyDomain = Object.freeze({
  None: 0,
  Application: 1 << 0,
  Infrastructure: 1 << 1,
  All: (1 << 0) | (1 << 1)
} as const);
export type ReadyDomainValue = typeof ReadyDomain[keyof typeof ReadyDomain];

/** Which kind of subject owns a ready-index entry. */
export const ReadyOwnerKind = Object.freeze({ Node: 1, Spot: 2, Actor: 3 } as const);
export type ReadyOwnerKindValue = typeof ReadyOwnerKind[keyof typeof ReadyOwnerKind];

/**
 * The kind of a received record. Maps one-to-one to the Core wire enum
 * `zlink_mesh_record_kind_t`; the addon passes these values through unchanged.
 */
export const ReceiveKind = Object.freeze({
  NodeSend: 1,
  NodeRequest: 2,
  ChannelSend: 3,
  ChannelRequest: 4,
  SpotSend: 5,
  SpotRequest: 6,
  SpotMulticast: 7,
  SpotControl: 8,
  ActorSend: 9,
  ActorRequest: 10,
  Completion: 11,
  SendReady: 12,
  TransferControl: 13
} as const);
export type ReceiveKindValue = typeof ReceiveKind[keyof typeof ReceiveKind];

/**
 * The operation a request or completion record concerns. Maps one-to-one to the
 * Core wire enum `zlink_mesh_operation_kind_t`; the addon passes these values
 * through unchanged.
 */
export const OperationKind = Object.freeze({
  NodeRequest: 1,
  ChannelRequest: 2,
  SpotRequest: 3,
  ActorRequest: 4,
  ActorLookup: 5,
  ActorDestroy: 6,
  ActorJoin: 7,
  ActorLeave: 8,
  StreamBind: 9,
  StreamUnbind: 10,
  StreamClose: 11
} as const);
export type OperationKindValue = typeof OperationKind[keyof typeof OperationKind];

/** An actor lifecycle transition kind (zlink_actor_lifecycle_kind_t). */
export const ActorLifecycleKind = Object.freeze({
  Created: 1,
  Joined: 2,
  Left: 3,
  Disconnected: 4,
  Destroyed: 5
} as const);
export type ActorLifecycleKindValue = typeof ActorLifecycleKind[keyof typeof ActorLifecycleKind];

/** Whether an actor join was accepted or rejected (zlink_actor_join_result_t). */
export const ActorJoinResult = Object.freeze({ Accepted: 0, Rejected: 1 } as const);
export type ActorJoinResultValue = typeof ActorJoinResult[keyof typeof ActorJoinResult];

/** Where a send-ready record says traffic may now be pushed (zlink_mesh_destination_kind_t). */
export const MeshDestinationKind = Object.freeze({
  Node: 1,
  Channel: 2,
  Spot: 3,
  Actor: 4,
  BoundSession: 5
} as const);
export type MeshDestinationKindValue = typeof MeshDestinationKind[keyof typeof MeshDestinationKind];

/**
 * The typed payload carried in a receive record's `kind_data`. Its concrete
 * shape is selected by the discriminating `kind` field, which mirrors the Core
 * `kind_data` mapping for the owning record.
 */
export type ReceiveKindData =
  | ActorControlPayload
  | ActorJoinCompletionPayload
  | ActorLookupCompletionPayload
  | SendReadyPayload
  | ActorTransferControlPayload;

/** The typed `kind_data` of a {@link ReceiveKind.SpotControl} record (zlink_actor_control_record_t). */
export interface ActorControlPayload {
  readonly kind: 'actorControl';
  readonly lifecycleKind: number;
  readonly previousActor: ActorRef | null;
  readonly currentActor: ActorRef | null;
  readonly previousSpotRid: RoutingId | null;
  readonly currentSpotRid: RoutingId | null;
  readonly previousSpotGeneration: bigint;
  readonly currentSpotGeneration: bigint;
  readonly previousMembershipEpoch: bigint;
  readonly currentMembershipEpoch: bigint;
  readonly resultCode: number;
}

/** The typed `kind_data` of an actor-join {@link ReceiveKind.Completion} record (zlink_actor_join_completion_t). */
export interface ActorJoinCompletionPayload {
  readonly kind: 'actorJoinCompletion';
  readonly joinResult: number;
  readonly actor: ActorRef | null;
  readonly location: ActorLocation;
}

/** The typed `kind_data` of an actor-lookup {@link ReceiveKind.Completion} record (zlink_actor_location_t). */
export interface ActorLookupCompletionPayload {
  readonly kind: 'actorLookupCompletion';
  readonly location: ActorLocation;
}

/** The typed `kind_data` of a {@link ReceiveKind.SendReady} record (zlink_mesh_send_ready_data_t). */
export interface SendReadyPayload {
  readonly kind: 'sendReady';
  readonly destinationKind: number;
  readonly targetNodeRid: RoutingId | null;
  readonly targetSpotRid: RoutingId | null;
  readonly targetActor: ActorRef | null;
  readonly channelName: string | null;
}

/** The typed `kind_data` of a {@link ReceiveKind.TransferControl} record (zlink_actor_transfer_control_t). */
export interface ActorTransferControlPayload {
  readonly kind: 'transferControl';
  readonly phase: number;
  readonly role: number;
  readonly transferId: ActorTransferId;
  readonly actor: ActorRef | null;
  readonly membershipEpoch: bigint;
  readonly finalSequence: bigint;
  readonly resultCode: number;
  readonly failureErrno: number;
}

/** One ready-index record surfaced by {@link MeshNode.drainReady}. */
export interface ReadyRecord {
  readonly ownerKind: number;
  readonly domain: number;
  readonly spotRid: RoutingId | null;
  readonly actor: ActorRef | null;
}

/** Batch capacity requirements returned when a receive batch is too small. */
export interface ReceiveRequirements {
  readonly messageCount: number;
  readonly partCount: number;
  readonly byteCount: number;
}

/** One materialized inbound record produced by claiming a ready entry. */
export interface ReceiveRecord {
  readonly kind: number;
  readonly domain: number;
  readonly sourceNodeRid: RoutingId | null;
  readonly sourceSpotRid: RoutingId | null;
  /** Bound-session generation validated for this application record, or zero. */
  readonly sourceBindingGeneration: bigint;
  readonly sourceActor: ActorRef | null;
  readonly operationId: MeshOperationId;
  readonly operationKind: number;
  readonly channelName: string | null;
  readonly topic: string | null;
  readonly applicationMetadata: Buffer | null;
  /** The typed `kind_data` for this record kind, or null when the kind carries none. */
  readonly kindData: ReceiveKindData | null;
  readonly terminalResult: number;
  readonly failureErrno: number;
  /** The received parts; the record owns them until they are consumed. */
  readonly parts: Message[];
  /** Reply to a replyable request record; returns the submit outcome. */
  reply(parts: MessageLike | readonly MessageLike[], flags?: number): SubmitResult;
  /** Reply to an actor-join request record with the join result. */
  replyActorJoin(joinResult: number, parts: MessageLike | readonly MessageLike[], flags?: number): SubmitResult;
}

/** The outcome of draining the ready index into a {@link ReadyBatch}. */
export interface DrainReadyResult {
  /** false when the drain would block and no records were produced. */
  readonly ok: boolean;
  readonly hasResidue: boolean;
  readonly records: ReadyRecord[];
}

/** The outcome of receiving into a {@link ReceiveBatch} from a {@link Claim}. */
export interface ClaimReceiveResult {
  /** false when the receive would block. */
  readonly ok: boolean;
  /** Set when the batch capacity was insufficient; retry with a larger batch. */
  readonly required?: ReceiveRequirements;
  readonly records: ReceiveRecord[];
}

/** A reusable batch that collects ready-index records for one drain pass. */
export interface ReadyBatch {
  /** The ready records produced by the last successful drain. */
  readonly records: ReadyRecord[];
  /** Take a claim over the ready record at `index` so its messages can be received. */
  takeClaim(index: number): Claim;
  /** Clear the batch for reuse. */
  reset(): void;
  /** Release native resources held by the batch. */
  close(): void;
}

/** A claim over one ready record; receive its messages, then release it. */
export interface Claim {
  /** Materialize the claimed messages into `batch`. */
  recvBatch(batch: ReceiveBatch, flags?: number): ClaimReceiveResult;
  /** Release the claim without receiving (or after receiving). */
  release(): void;
}

/** A reusable batch that materializes received messages for one claim. */
export interface ReceiveBatch {
  /** Clear the batch for reuse. */
  reset(): void;
  /** Release native resources held by the batch. */
  close(): void;
}
