// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from '../buffer_like';
import {
  Message,
  Received,
  Subscribed,
  SubscriptionEvent,
  type MessageSnapshot
} from '../message';
import { requireNative } from '../native';
import { ReceiveFlag } from './constants';
import type { BufferLike } from '../buffer_like';
import type { MessageLike } from '../message';

interface RecvSocketLike {
  nativeHandle(): unknown;
}

export function normalizeMessagePayload(
  message: MessageLike,
  label = 'message'
): Buffer | MessageSnapshot {
  return message instanceof Message
    ? message.toSnapshot()
    : normalizeBufferLike(message, label);
}

export function normalizeMultipart(parts: readonly MessageLike[]): Array<Buffer | MessageSnapshot> {
  if (!Array.isArray(parts)) throw new TypeError('parts must be an array');
  return parts.map((part, index) => normalizeMessagePayload(part, `parts[${index}]`));
}

export function recvMessage(socket: RecvSocketLike, flags?: number): Received;
export function recvMessage(socket: RecvSocketLike, size: number, flags: number): Buffer;
export function recvMessage(
  socket: RecvSocketLike,
  arg0 = 0,
  arg1?: number
): Received | Buffer {
  if (typeof arg1 === 'number') {
    return requireNative().socketRecv(socket.nativeHandle(), arg0 | 0, arg1 | 0) as Buffer;
  }
  if (typeof arg0 === 'number' && arg0 > ReceiveFlag.DONTWAIT) {
    return requireNative().socketRecv(socket.nativeHandle(), arg0 | 0, 0) as Buffer;
  }
  const raw = requireNative().socketRecvMessage(socket.nativeHandle(), arg0 | 0) as {
    parts: MessageSnapshot[];
    routingId?: Buffer | null;
    hasMore?: boolean;
  };
  return materializeReceived(raw);
}

export function recvInto(
  socket: RecvSocketLike,
  buffer: BufferLike,
  flags = 0
): number {
  return requireNative().socketRecvInto(
    socket.nativeHandle(),
    normalizeBufferLike(buffer, 'buffer'),
    flags | 0
  ) as number;
}

export function recvMsgInto(
  socket: RecvSocketLike,
  buffer: BufferLike,
  flags = 0
): number {
  return requireNative().socketRecvMsgInto(
    socket.nativeHandle(),
    normalizeBufferLike(buffer, 'buffer'),
    flags | 0
  ) as number;
}

export { normalizeBufferLike };

export function wrapMessageParts(parts: readonly Buffer[]): Message[] {
  return parts.map((part) => Message.fromBuffer(part));
}

export function materializeReceived(raw: {
  parts: readonly MessageSnapshot[];
  routingId?: Buffer | null;
  requestSeq?: bigint | null;
}): Received {
  return new Received(
    materializeMessageParts(raw.parts),
    raw.routingId ?? null,
    raw.requestSeq ?? null
  );
}

export function materializeSubscribed(raw: {
  topic: string;
  parts: readonly MessageSnapshot[];
  routingId?: Buffer | null;
}): Subscribed {
  return new Subscribed(
    raw.topic,
    materializeMessageParts(raw.parts),
    raw.routingId ?? null
  );
}

export function materializeSubscriptionEvent(raw: {
  topic: string;
  subscribed: boolean;
  routingId?: Buffer | null;
}): SubscriptionEvent {
  return new SubscriptionEvent(raw.topic, raw.subscribed, raw.routingId ?? null);
}

function materializeMessageParts(parts: readonly MessageSnapshot[]): readonly Message[] {
  return Object.freeze(parts.map((part) => Message.fromSnapshot(part)));
}
