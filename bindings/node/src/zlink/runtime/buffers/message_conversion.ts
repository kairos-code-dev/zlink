// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from '../../contracts/core/buffer_like';
import {
  Message,
  type MessageLike,
  type MessageSnapshot
} from '../../contracts';

export function toMessageParts(parts: readonly MessageLike[]): Array<Buffer | MessageSnapshot> {
  return parts.map((part, index) =>
    part instanceof Message ? part.toSnapshot() : normalizeBufferLike(part, `parts[${index}]`)
  );
}

export function normalizeMessageLikePayload(message: MessageLike | readonly MessageLike[]): Buffer | MessageSnapshot | Array<Buffer | MessageSnapshot> {
  if (Array.isArray(message)) {
    if (message.length === 1) {
      return normalizeMessageLikePayload(message[0]);
    }
    return toMessageParts(message);
  }
  const scalar = message as MessageLike;
  return scalar instanceof Message ? scalar.toSnapshot() : normalizeBufferLike(scalar, 'message');
}

export function normalizeOperationPayload(parts: MessageLike | readonly MessageLike[]): Buffer | MessageSnapshot | Array<Buffer | MessageSnapshot> {
  if (!Array.isArray(parts)) {
    const scalar = parts as MessageLike;
    return scalar instanceof Message ? scalar.toSnapshot() : normalizeBufferLike(scalar, 'message');
  }
  if (parts.length === 1) {
    const part = parts[0];
    return part instanceof Message ? part.toSnapshot() : normalizeBufferLike(part, 'message');
  }
  return toMessageParts(parts);
}

export function toOwnedMessage(message: MessageLike): Message {
  return message instanceof Message ? message : Message.from(message);
}

export function messageFromNativeBuffer(buffer: Buffer | null | undefined): Message {
  return Message.fromSnapshot({ data: buffer ?? Buffer.alloc(0) });
}
