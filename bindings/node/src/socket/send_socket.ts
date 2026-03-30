// SPDX-License-Identifier: MPL-2.0

import { requireNative } from '../native';
import { BaseSocket } from './base_socket';
import {
  normalizeBufferLike,
  normalizeMessagePayload,
  normalizeMultipart
} from './socket_support';
import type { BufferLike } from '../buffer_like';
import type { MessageLike } from '../message';

export class SendSocket extends BaseSocket {
  protected constructor(ctx: import('../index').Context, type: number) {
    super(ctx, type);
  }

  send(message: MessageLike, flags = 0): number {
    return requireNative().socketSend(
      this.nativeHandle(),
      normalizeMessagePayload(message),
      flags | 0
    ) as number;
  }

  sendParts(parts: readonly MessageLike[], flags = 0): number {
    return requireNative().socketSendParts(
      this.nativeHandle(),
      normalizeMultipart(parts),
      flags | 0
    ) as number;
  }

  sendFrom(buffer: BufferLike, length: number, flags = 0): number {
    return requireNative().socketSendFrom(
      this.nativeHandle(),
      normalizeBufferLike(buffer, 'buffer'),
      length | 0,
      flags | 0
    ) as number;
  }
}
