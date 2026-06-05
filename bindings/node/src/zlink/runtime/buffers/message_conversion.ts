// SPDX-License-Identifier: MPL-2.0

import {
  Message,
  type MessageLike
} from '../../contracts';
import {
  messageFromSnapshot,
  messageToSnapshot,
  type MessageSnapshot
} from '../messaging/message_snapshot';

function normalizeBufferLike(value: MessageLike, label = 'value'): Buffer {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (typeof value === 'string') return Buffer.from(value);
  throw new TypeError(`${label} must be Buffer, Uint8Array, or string`);
}

export function toMessageParts(parts: readonly MessageLike[]): Array<Buffer | MessageSnapshot> {
  return parts.map((part, index) =>
    part instanceof Message ? messageToSnapshot(part) : normalizeBufferLike(part, `parts[${index}]`)
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
  return scalar instanceof Message ? messageToSnapshot(scalar) : normalizeBufferLike(scalar, 'message');
}

export function normalizeOperationPayload(parts: MessageLike | readonly MessageLike[]): Buffer | MessageSnapshot | Array<Buffer | MessageSnapshot> {
  if (!Array.isArray(parts)) {
    const scalar = parts as MessageLike;
    return scalar instanceof Message ? messageToSnapshot(scalar) : normalizeBufferLike(scalar, 'message');
  }
  if (parts.length === 1) {
    const part = parts[0];
    return part instanceof Message ? messageToSnapshot(part) : normalizeBufferLike(part, 'message');
  }
  return toMessageParts(parts);
}

export function messageFromNativeBuffer(buffer: Buffer | null | undefined): Message {
  return messageFromSnapshot({ data: buffer ?? Buffer.alloc(0) });
}
