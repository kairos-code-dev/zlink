// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { ActorRef, ActorTransferId } from './dispatch';

/** The side of an actor transfer fence (zlink_actor_transfer_role_t). */
export const ActorTransferRole = Object.freeze({ Source: 1, Target: 2 } as const);
export type ActorTransferRoleValue = typeof ActorTransferRole[keyof typeof ActorTransferRole];

/** A phase in the actor transfer fence protocol (zlink_actor_transfer_phase_t). */
export const ActorTransferPhase = Object.freeze({
  Preparing: 1,
  Fenced: 2,
  Committed: 3,
  Activated: 4,
  Aborted: 5
} as const);
export type ActorTransferPhaseValue = typeof ActorTransferPhase[keyof typeof ActorTransferPhase];

/**
 * An opaque, framework-owned actor transfer fence token returned by
 * {@link MeshNode.prepareActorTransfer} and passed to commit, activate, or
 * abort. Maps to `zlink_actor_transfer_token_t`; the bytes are not to be
 * inspected or mutated by the consumer.
 */
export interface ActorTransferToken {
  readonly opaque: Buffer;
}

/** Inputs to {@link MeshNode.prepareActorTransfer} (zlink_actor_transfer_prepare_t). */
export interface ActorTransferPrepare {
  readonly role: number;
  readonly transferId: ActorTransferId;
  readonly actor: ActorRef;
  readonly expectedMembershipEpoch: bigint;
  readonly peerNodeRid: RoutingId;
  readonly finalSequence: bigint;
  readonly reserveMessageCount: bigint;
  readonly reserveByteCount: bigint;
}

/** The result detail of {@link MeshNode.prepareActorTransfer} (zlink_actor_transfer_prepare_result_t). */
export interface ActorTransferPrepareResult {
  readonly role: number;
  readonly transferId: ActorTransferId;
  readonly actor: ActorRef;
  readonly finalSequence: bigint;
  readonly reserveMessageCount: bigint;
  readonly reserveByteCount: bigint;
}

/** The outcome of {@link MeshNode.prepareActorTransfer}: the fence token and its result detail. */
export interface PrepareActorTransferResult {
  readonly token: ActorTransferToken;
  readonly result: ActorTransferPrepareResult;
}
