// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from './buffer_like';
import type { BufferLike } from './buffer_like';

/** Message type constants for request-reply envelope. */
export const MsgType = Object.freeze({
  Data: 0,
  Request: 1,
  Reply: 2,
} as const);

/** Minimum user-defined metadata key. */
export const METADATA_KEY_USER_MIN = 0x0100;
/** Maximum metadata value size in bytes. */
export const METADATA_VALUE_MAX = 65535;

/** Request-reply information extracted from a message. */
export interface RequestInfo {
  readonly msgType: number;
  readonly correlationId: bigint;
}

/** @internal */
export interface MessageSnapshot {
  data: Buffer;
  refCount?: number;
  properties?: Readonly<Record<string, string>>;
  requestInfo?: RequestInfo;
  metadata?: Readonly<Map<number, Buffer>>;
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
const EMPTY_METADATA: Readonly<Map<number, Buffer>> = Object.freeze(new Map<number, Buffer>());

export class Message {
  private readonly _buffer: Buffer;
  private readonly _refCount: number;
  private readonly _properties: Readonly<Record<string, string>>;
  private readonly _requestInfo: RequestInfo;
  private readonly _metadata: Readonly<Map<number, Buffer>>;

  private constructor(
    buffer: Buffer,
    refCount = 1,
    properties?: Readonly<Record<string, string>>,
    requestInfo?: RequestInfo,
    metadata?: Readonly<Map<number, Buffer>>
  ) {
    this._buffer = buffer;
    this._refCount = refCount | 0;
    this._properties = normalizeMessageProperties(properties);
    this._requestInfo = requestInfo ?? { msgType: MsgType.Data, correlationId: BigInt(0) };
    this._metadata = metadata ?? EMPTY_METADATA;
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
      snapshot.properties,
      snapshot.requestInfo,
      snapshot.metadata
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

  /** Get request-reply information (msgType and correlationId). */
  getRequestInfo(): RequestInfo {
    return this._requestInfo;
  }

  /** Get a metadata value by key. Returns null if absent. */
  getMetadata(key: number): Buffer | null {
    return this._metadata.get(key) ?? null;
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
