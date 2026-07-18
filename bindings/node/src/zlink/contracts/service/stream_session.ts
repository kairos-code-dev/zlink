// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { MessageLike } from '../messaging';
import type { RequestResult, SubmitResult } from '../errors';
import type { ActorRef, MeshOperationId } from './dispatch';
import type { MessagingOptions, RequestOptions } from './shared';

/** A point-in-time snapshot of a stream-session service. */
export interface StreamSessionStatus {
  readonly state: number;
  readonly lifecycleGeneration: bigint;
  readonly sessionCount: bigint;
  readonly bindingCount: bigint;
  readonly pendingMessageCount: bigint;
  readonly pendingByteCount: bigint;
  readonly lastError: number;
}

/** One actor-to-session binding held by the service. */
export interface StreamSessionBinding {
  readonly sessionRid: RoutingId;
  readonly actor: ActorRef;
  readonly bindingGeneration: bigint;
  readonly membershipEpoch: bigint;
}

/**
 * Bridges actors onto raw STREAM sessions so external TCP peers can address
 * mesh actors by session routing id.
 */
export interface StreamSessionService {
  /** Start the service. */
  start(): void;
  /** Shut the service down, waiting up to `timeoutMs`; returns the outcome. */
  shutdown(timeoutMs: number): RequestResult;
  /** Release native resources held by the service. */
  close(): void;
  /** Return a fresh status snapshot. */
  status(): StreamSessionStatus;

  /** Bind `actor` to session `sessionRid`; completion arrives through pull dispatch. */
  bindActor(sessionRid: RoutingId, actor: ActorRef, timeoutMs?: number): MeshOperationId;
  /** Remove `actor`'s binding from `sessionRid`; completion arrives through pull dispatch. */
  unbindActor(
    sessionRid: RoutingId,
    actor: ActorRef,
    expectedBindingGeneration: bigint,
    timeoutMs?: number
  ): MeshOperationId;
  /** Return the bindings currently held for `sessionRid`. */
  bindings(sessionRid: RoutingId): StreamSessionBinding[];

  /** Send parts to `actor` over `sessionRid`; returns the submit outcome. */
  sendToActor(
    sessionRid: RoutingId,
    actor: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): SubmitResult;
  /** Request to `actor` over `sessionRid`; completion arrives through pull dispatch. */
  requestToActor(
    sessionRid: RoutingId,
    actor: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: RequestOptions
  ): MeshOperationId;
}
