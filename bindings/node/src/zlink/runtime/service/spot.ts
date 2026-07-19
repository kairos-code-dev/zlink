// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';
import type { MessageLike } from '../../contracts/messaging';
import type { SubmitResult } from '../../contracts/errors';
import type {
  MeshOperationId,
  MeshPublishDetail,
  MessagingOptions,
  RequestOptions,
  Spot as SpotContract,
  SpotStatus
} from '../../contracts/service';
import { SubscriptionKind } from '../../contracts/service';
import type { MeshPublishDetailRaw, SpotStatusRaw } from '../native/binding_service_types';
import { NativeHandle, getNativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import { closeCall, configCall } from '../errors/native_errors';
import { normalizeRoutingId } from '../core/routing_id';
import { normalizeMessageLikePayload } from '../buffers/message_conversion';
import { flagsOrZero, metadataOrNull, timeoutOrZero } from './conversions';

/** Runtime spot: wraps a native spot handle owned by a {@link RuntimeMeshNode}. */
export class Spot extends NativeHandle implements SpotContract {
  constructor(native: unknown) {
    super(native);
  }

  get routingId(): RoutingId | null {
    if (getNativeHandle(this) == null) return null;
    return this.status().routingId;
  }

  status(): SpotStatus {
    const raw: SpotStatusRaw = configCall('spot status failed', () =>
      requireNative().spotStatus(getNativeHandle(this))
    );
    return {
      routingId: RoutingId.from(raw.spotRid),
      kind: raw.spotKind,
      lifecycleGeneration: raw.lifecycleGeneration,
      pendingApplicationMessages: raw.pendingApplicationMessages,
      pendingInfrastructureMessages: raw.pendingInfrastructureMessages,
      pendingBytes: raw.pendingBytes,
      activeActorCount: raw.activeActorCount,
      draining: raw.draining !== 0,
      lastError: raw.lastError,
      lastChangedMs: raw.lastChangedMs
    };
  }

  sendToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult {
    return requireNative().spotSendToChannel(
      getNativeHandle(this),
      channelName,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  requestToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId {
    return requireNative().spotRequestToChannel(
      getNativeHandle(this),
      channelName,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }

  sendToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): SubmitResult {
    return requireNative().spotSendToSpot(
      getNativeHandle(this),
      normalizeRoutingId(targetNodeRid, 'targetNodeRid'),
      normalizeRoutingId(targetSpotRid, 'targetSpotRid'),
      targetSpotGeneration,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  requestToSpot(
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    targetSpotGeneration: bigint,
    parts: MessageLike | readonly MessageLike[],
    options?: RequestOptions
  ): MeshOperationId {
    return requireNative().spotRequestToSpot(
      getNativeHandle(this),
      normalizeRoutingId(targetNodeRid, 'targetNodeRid'),
      normalizeRoutingId(targetSpotRid, 'targetSpotRid'),
      targetSpotGeneration,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }

  publish(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): MeshPublishDetail {
    return requireNative().spotPublish(
      getNativeHandle(this),
      channelName,
      topic,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as MeshPublishDetailRaw as MeshPublishDetail;
  }

  setSubscription(channelName: string, topicFilter: string, kind: number = SubscriptionKind.Exact): void {
    configCall('spot subscription set failed', () => {
      requireNative().spotSetSubscription(getNativeHandle(this), channelName, topicFilter, kind | 0);
    });
  }

  unsetSubscription(channelName: string, topicFilter: string, kind: number = SubscriptionKind.Exact): void {
    configCall('spot subscription unset failed', () => {
      requireNative().spotUnsetSubscription(getNativeHandle(this), channelName, topicFilter, kind | 0);
    });
  }

  close(): void {
    if (getNativeHandle(this) != null) {
      closeCall('spot close failed', () => {
        requireNative().spotDestroy(getNativeHandle(this));
      });
      this._native = null;
    }
  }
}
