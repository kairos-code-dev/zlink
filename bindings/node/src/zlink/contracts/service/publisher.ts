// SPDX-License-Identifier: MPL-2.0

import type { MessageLike } from '../messaging';
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

/** A publisher that fans out topic messages to subscribing spots. */
export interface Publisher {
  /** Publish parts to a channel topic; returns the publish accounting detail. */
  publish(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): MeshPublishDetail;
  /** Apply a publisher option. */
  setOption(option: number, value: Buffer): void;
  /** Read a publisher option. */
  getOption(option: number): Buffer;
  /** Release native resources held by the publisher. */
  close(): void;
}
