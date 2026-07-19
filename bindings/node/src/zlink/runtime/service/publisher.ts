// SPDX-License-Identifier: MPL-2.0

import type { MessageLike } from '../../contracts/messaging';
import type {
  MeshPublishDetail,
  MessagingOptions,
  Publisher as PublisherContract
} from '../../contracts/service';
import type { MeshPublishDetailRaw } from '../native/binding_service_types';
import { NativeHandle, getNativeHandle } from '../handles/native_handle';
import { requireNative } from '../native/native';
import { closeCall } from '../errors/native_errors';
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

  close(): void {
    if (getNativeHandle(this) != null) {
      closeCall('publisher close failed', () => {
        requireNative().meshNodePublisherDestroy(getNativeHandle(this));
      });
      this._native = null;
    }
  }
}
