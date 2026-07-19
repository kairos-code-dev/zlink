// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { MessageLike } from '../messaging';
import type { SubmitResult } from '../errors';
import type { MeshOperationId } from './dispatch';
import type { MessagingOptions, RequestOptions } from './shared';
import type { MeshPublishDetail } from './publisher';

/** The kind of a spot (entry or user). */
export const SpotKind = Object.freeze({ Invalid: 0, Entry: 1, User: 2 } as const);
export type SpotKindValue = typeof SpotKind[keyof typeof SpotKind];

/** How a subscription topic filter is matched. */
export const SubscriptionKind = Object.freeze({ Exact: 1, Prefix: 2 } as const);
export type SubscriptionKindValue = typeof SubscriptionKind[keyof typeof SubscriptionKind];

/** A point-in-time snapshot of a spot's state. */
export interface SpotStatus {
  readonly routingId: RoutingId;
  readonly kind: number;
  readonly lifecycleGeneration: bigint;
  readonly pendingApplicationMessages: bigint;
  readonly pendingInfrastructureMessages: bigint;
  readonly pendingBytes: bigint;
  readonly activeActorCount: number;
  readonly draining: boolean;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

/**
 * A spot: an addressable participant of the mesh that subscribes to channels,
 * publishes topics, and exchanges messages with peer spots.
 */
export interface Spot {
  /** This spot's routing id, or null once it has been closed. */
  readonly routingId: RoutingId | null;
  /** Return a fresh status snapshot. */
  status(): SpotStatus;

  /** Send parts to a channel; returns the submit outcome. */
  sendToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult;
  /** Request over a channel; completion arrives through pull dispatch. */
  requestToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId;

  /** Send parts to a specific peer spot; returns the submit outcome. */
  sendToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): SubmitResult;
  /** Request to a specific peer spot; completion arrives through pull dispatch. */
  requestToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[],
    options?: RequestOptions
  ): MeshOperationId;

  /** Publish parts to a channel topic; returns the publish accounting detail. */
  publish(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): MeshPublishDetail;
  /** Subscribe to a channel topic filter. */
  setSubscription(channelName: string, topicFilter: string, kind?: number): void;
  /** Remove a channel topic subscription. */
  unsetSubscription(channelName: string, topicFilter: string, kind?: number): void;

  /** Release native resources held by the spot. */
  close(): void;
}
