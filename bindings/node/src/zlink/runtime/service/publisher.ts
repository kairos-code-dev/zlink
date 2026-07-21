// SPDX-License-Identifier: MPL-2.0

import type { MessageLike } from '../../contracts/messaging';
import { SubmitError, SubmitResult } from '../../contracts/errors';
import type {
  MeshPublishDetail,
  MeshPublishResult,
  MessagingOptions,
  Publisher as PublisherContract
} from '../../contracts/service';
import type { MeshPublishDetailRaw } from '../native/binding_service_types';
import { NativeHandle, getNativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import { closeCall } from '../errors/native_errors';
import { withRuntimeErrorMessage } from '../errors/error_state';
import { normalizeMessageLikePayload } from '../buffers/message_conversion';
import { flagsOrZero, metadataOrNull } from './conversions';

/** Runtime publisher: wraps a native publisher handle owned by a mesh node. */
export class Publisher extends NativeHandle implements PublisherContract {
  constructor(native: unknown) {
    super(native);
  }

  publish(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): MeshPublishDetail {
    return requireNative().meshNodePublisherPublish(
      getNativeHandle(this),
      channelName,
      topic,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    ) as MeshPublishDetailRaw as MeshPublishDetail;
  }

  async publishAsync(
    channelName: string,
    topic: string,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions,
    signal?: AbortSignal
  ): Promise<MeshPublishResult> {
    if (signal?.aborted === true) {
      throw abortReason(signal);
    }
    const native = requireNative();
    const operation = native.meshNodePublisherPublishAsync(
      getNativeHandle(this),
      channelName,
      topic,
      metadataOrNull(options?.metadata),
      normalizeMessageLikePayload(parts),
      flagsOrZero(options?.flags)
    );
    let cancelledReason: unknown;
    const onAbort = (): void => {
      if (native.meshNodePublisherPublishCancel(operation.cancelToken)) {
        cancelledReason = abortReason(signal!);
      }
    };
    signal?.addEventListener('abort', onAbort, { once: true });
    try {
      const result = await operation.promise;
      if (result.cancelled) {
        throw cancelledReason ?? abortReason(signal);
      }
      if (!isPublishOutcomeResult(result.result)) {
        throw withRuntimeErrorMessage(
          new SubmitError(result.result as SubmitResult, result.nativeErrno),
          `meshNodePublisherPublishAsync failed: ${result.errorMessage}`
        );
      }
      return {
        result: result.result as SubmitResult,
        detail: result.detail as MeshPublishDetail
      };
    } finally {
      signal?.removeEventListener('abort', onAbort);
    }
  }

  close(): void {
    if (getNativeHandle(this) != null) {
      closeCall('publisher close failed', () => {
        requireNative().meshNodePublisherDestroy(getNativeHandle(this));
      });
      this._native = null;
    }
  }
}

function abortReason(signal?: AbortSignal): unknown {
  return signal?.reason ?? new DOMException('The operation was aborted.', 'AbortError');
}

function isPublishOutcomeResult(result: number): boolean {
  return result === SubmitResult.Ok
    || result === SubmitResult.Backpressured
    || result === SubmitResult.NotConnected
    || result === SubmitResult.NotFound
    || result === SubmitResult.Terminated
    || result === SubmitResult.NotAdmitted;
}
