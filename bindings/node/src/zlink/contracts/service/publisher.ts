// SPDX-License-Identifier: MPL-2.0

import type { MessageLike } from '../messaging';
import type { SubmitResult } from '../errors';
import type { MessagingOptions } from './shared';

/** Accounting detail returned by a publish, describing its fan-out. */
export interface MeshPublishDetail {
  readonly snapshotRemoteTargetCount: number;
  readonly admittedRemoteTargetCount: number;
  readonly droppedRemoteTargetCount: number;
  readonly unreachableRemoteTargetCount: number;
  readonly snapshotLocalSpotCount: number;
  readonly admittedLocalSpotCount: number;
  readonly droppedLocalSpotCount: number;
}

/** The terminal submit result and accounting detail from one publish. */
export interface MeshPublishResult {
  readonly result: SubmitResult;
  readonly detail: MeshPublishDetail;
}

/** A publisher that fans out topic messages to subscribing spots. */
export interface Publisher {
  /** Publish parts to a channel topic; returns the publish accounting detail. */
  publish(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): MeshPublishDetail;
  /**
   * Publish without blocking the Node.js event loop.
   *
   * Cancellation is accepted only before the native Core call starts. Once
   * Core starts the publish, the promise preserves that publish's final result
   * and detail. Programming and system failures remain exceptional.
   */
  publishAsync(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions,
    signal?: AbortSignal
  ): Promise<MeshPublishResult>;
  /**
   * Close the publisher without blocking the event loop. New operations are
   * rejected immediately; an already queued or started async publish retains
   * the native handle until its Core call and promise completion finish.
   */
  close(): void;
}
