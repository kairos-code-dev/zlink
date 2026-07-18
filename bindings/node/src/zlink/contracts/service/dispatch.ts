// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { Message } from '../messaging';
import type { SubmitResult } from '../errors';

/** A 128-bit mesh operation id whose completion arrives through pull dispatch. */
export interface MeshOperationId {
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

/** Which kind of subject owns a ready-index entry. */
export const ReadyOwnerKind = Object.freeze({ Node: 1, Spot: 2, Actor: 3 } as const);
export type ReadyOwnerKindValue = typeof ReadyOwnerKind[keyof typeof ReadyOwnerKind];

/** Which kind of subject a received record concerns. */
export const ReceiveKind = Object.freeze({ Node: 1, Spot: 2, Actor: 3 } as const);
export type ReceiveKindValue = typeof ReceiveKind[keyof typeof ReceiveKind];

/** The messaging shape a received record carries. */
export const OperationKind = Object.freeze({
  Send: 1,
  Request: 2,
  Reply: 3,
  Publish: 4,
  ActorJoin: 5,
  ActorLeave: 6
} as const);
export type OperationKindValue = typeof OperationKind[keyof typeof OperationKind];

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
  readonly sourceActor: ActorRef | null;
  readonly operationId: MeshOperationId;
  readonly operationKind: number;
  readonly channelName: string | null;
  readonly topic: string | null;
  readonly applicationMetadata: Buffer | null;
  readonly terminalResult: number;
  readonly failureErrno: number;
  /** The received parts; the record owns them until they are consumed. */
  readonly parts: Message[];
  /** Reply to a replyable request record; returns the submit outcome. */
  reply(parts: Message | readonly Message[], flags?: number): SubmitResult;
  /** Reply to an actor-join request record with the join result. */
  replyActorJoin(joinResult: number, parts: Message | readonly Message[], flags?: number): SubmitResult;
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
