// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from './buffer_like';
import type { BufferLike } from './buffer_like';

/** @internal */
export interface MessageSnapshot {
  data: Buffer;
  refCount?: number;
  properties?: Readonly<Record<string, string>>;
}

function normalizeMessageProperties(
  properties?: Readonly<Record<string, string>>
): Readonly<Record<string, string>> {
  if (!properties) {
    return EMPTY_PROPERTIES;
  }
  return Object.isFrozen(properties) ? properties : Object.freeze(properties);
}

const EMPTY_PROPERTIES: Readonly<Record<string, string>> = Object.freeze({});

export class Message {
  private readonly _buffer: Buffer;
  private readonly _refCount: number;
  private readonly _properties: Readonly<Record<string, string>>;

  private constructor(
    buffer: Buffer,
    refCount = 1,
    properties?: Readonly<Record<string, string>>
  ) {
    this._buffer = buffer;
    this._refCount = refCount | 0;
    this._properties = normalizeMessageProperties(properties);
    Object.freeze(this);
  }

  static fromBuffer(buffer: BufferLike): Message {
    return new Message(normalizeBufferLike(buffer, 'buffer'));
  }

  /** @internal */
  static fromSnapshot(snapshot: MessageSnapshot): Message {
    return new Message(
      snapshot.data,
      snapshot.refCount ?? 1,
      snapshot.properties
    );
  }

  /** @internal */
  payloadBuffer(): Buffer {
    return this._buffer;
  }

  get data(): Buffer {
    return this._buffer;
  }

  get size(): number {
    return this._buffer.length;
  }

  getProperty(name: string): string | null {
    if (typeof name !== 'string') {
      throw new TypeError('property name must be a string');
    }
    return Object.prototype.hasOwnProperty.call(this._properties, name)
      ? this._properties[name]
      : null;
  }

  refCount(): number {
    return this._refCount;
  }

  close(): void {}

  [Symbol.dispose](): void {
    this.close();
  }

  async [Symbol.asyncDispose](): Promise<void> {
    this.close();
  }
}

export class Received {
  readonly parts: readonly Message[];
  readonly routingId: Buffer | null;

  constructor(parts: readonly Message[], routingId: Buffer | null = null) {
    this.parts = Object.isFrozen(parts) ? parts : Object.freeze(parts.slice());
    this.routingId = routingId;
  }

  singlePartOrThrow(): Message {
    if (this.parts.length !== 1) {
      throw new Error(`expected exactly 1 part but received ${this.parts.length}`);
    }
    return this.parts[0];
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
    this.parts = Object.isFrozen(parts) ? parts : Object.freeze(parts.slice());
  }

  singlePartOrThrow(): Message {
    if (this.parts.length !== 1) {
      throw new Error(`expected exactly 1 part but received ${this.parts.length}`);
    }
    return this.parts[0];
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
