// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';
import type { MessageLike } from '../../contracts/messaging';
import type { RequestResult, SubmitResult } from '../../contracts/errors';
import type {
  ActorRef,
  MeshOperationId,
  MessagingOptions,
  RequestOptions,
  StreamSessionBinding,
  StreamSessionService as StreamSessionServiceContract,
  StreamSessionStatus
} from '../../contracts/service';
import type {
  StreamSessionBindingRaw,
  StreamSessionStatusRaw
} from '../native/binding_service_types';
import { NativeHandle, getNativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import { closeCall, configCall } from '../errors/native_errors';
import { normalizeRoutingId } from '../core/routing_id';
import { normalizeMessageLikePayload } from '../buffers/message_conversion';
import {
  actorRefFromRaw,
  actorRefToRaw,
  flagsOrZero,
  metadataOrNull,
  timeoutOrZero
} from './conversions';

/** Runtime stream-session service: wraps a native service handle. */
export class StreamSessionService extends NativeHandle implements StreamSessionServiceContract {
  constructor(native: unknown) {
    super(native);
  }

  start(): void {
    configCall('stream session service start failed', () => {
      requireNative().streamSessionServiceStart(getNativeHandle(this));
    });
  }

  shutdown(timeoutMs: number): RequestResult {
    return requireNative().streamSessionServiceShutdown(getNativeHandle(this), timeoutOrZero(timeoutMs)) as RequestResult;
  }

  close(): void {
    if (getNativeHandle(this) != null) {
      closeCall('stream session service close failed', () => {
        requireNative().streamSessionServiceDestroy(getNativeHandle(this));
      });
      this._native = null;
    }
  }

  status(): StreamSessionStatus {
    return configCall('stream session service status failed', () =>
      requireNative().streamSessionServiceStatus(getNativeHandle(this))
    ) as StreamSessionStatusRaw as StreamSessionStatus;
  }

  bindActor(sessionRid: RoutingId, actor: ActorRef, timeoutMs?: number): MeshOperationId {
    return requireNative().streamSessionBindActor(
      getNativeHandle(this),
      normalizeRoutingId(sessionRid, 'sessionRid'),
      actorRefToRaw(actor),
      timeoutOrZero(timeoutMs)
    );
  }

  unbindActor(
    sessionRid: RoutingId,
    actor: ActorRef,
    expectedBindingGeneration: bigint,
    timeoutMs?: number
  ): MeshOperationId {
    return requireNative().streamSessionUnbindActor(
      getNativeHandle(this),
      normalizeRoutingId(sessionRid, 'sessionRid'),
      actorRefToRaw(actor),
      expectedBindingGeneration,
      timeoutOrZero(timeoutMs)
    );
  }

  bindings(sessionRid: RoutingId): StreamSessionBinding[] {
    const raw: StreamSessionBindingRaw[] = configCall('stream session bindings failed', () =>
      requireNative().streamSessionBindings(getNativeHandle(this), normalizeRoutingId(sessionRid, 'sessionRid'))
    );
    return raw.map((entry) => ({
      sessionRid: RoutingId.from(entry.sessionRid),
      actor: actorRefFromRaw(entry.actor),
      bindingGeneration: entry.bindingGeneration,
      membershipEpoch: entry.membershipEpoch
    }));
  }

  sendToActor(
    sessionRid: RoutingId,
    actor: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): SubmitResult {
    return requireNative().streamSessionSendToActor(
      getNativeHandle(this),
      normalizeRoutingId(sessionRid, 'sessionRid'),
      actorRefToRaw(actor),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as SubmitResult;
  }

  requestToActor(
    sessionRid: RoutingId,
    actor: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: RequestOptions
  ): MeshOperationId {
    return requireNative().streamSessionRequestToActor(
      getNativeHandle(this),
      normalizeRoutingId(sessionRid, 'sessionRid'),
      actorRefToRaw(actor),
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags),
      timeoutOrZero(options?.timeoutMs)
    );
  }
}
