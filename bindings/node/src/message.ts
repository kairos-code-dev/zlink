// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from './buffer_like';
import type { BufferLike } from './buffer_like';

export class Message {
  private readonly _buffer: Buffer;
  private readonly _borrowed: boolean;

  private constructor(buffer: Buffer, borrowed: boolean) {
    this._buffer = buffer;
    this._borrowed = borrowed === true;
    Object.freeze(this);
  }

  static copyOf(data: BufferLike | string, encoding: BufferEncoding = 'utf8'): Message {
    if (typeof data === 'string') return new Message(Buffer.from(data, encoding), false);
    return new Message(Buffer.from(normalizeBufferLike(data, 'data')), false);
  }

  static wrap(buffer: BufferLike): Message {
    return new Message(normalizeBufferLike(buffer, 'buffer'), true);
  }

  static empty(): Message {
    return new Message(Buffer.alloc(0), false);
  }

  /** @internal */
  payloadBuffer(): Buffer {
    return this._buffer;
  }

  toBuffer(): Buffer {
    return this._borrowed ? this._buffer : Buffer.from(this._buffer);
  }

  byteLength(): number {
    return this._buffer.length;
  }
}

export class Received {
  readonly parts: readonly Message[];
  readonly routingId: Buffer | null;

  constructor(parts: readonly Message[], routingId: Buffer | null = null) {
    this.parts = Object.freeze(parts.slice());
    this.routingId = routingId;
  }

  close(): void {}
}

export class Subscribed {
  readonly routingId: Buffer | null;
  readonly topic: string;
  readonly parts: readonly Message[];

  constructor(topic: string, parts: readonly Message[], routingId: Buffer | null = null) {
    this.routingId = routingId;
    this.topic = topic;
    this.parts = Object.freeze(parts.slice());
  }

  close(): void {}
}

export class SubscriptionEvent {
  readonly routingId: Buffer | null;
  readonly topic: string;
  readonly subscribed: boolean;

  constructor(topic: string, subscribed: boolean, routingId: Buffer | null = null) {
    this.routingId = routingId;
    this.topic = topic;
    this.subscribed = subscribed === true;
  }
}

export type MessageLike = Message | BufferLike | string;
