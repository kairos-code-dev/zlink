// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from '../core/buffer_like';
import type { BufferLike } from '../core/buffer_like';

/** Minimum user-defined metadata key. */
export const METADATA_KEY_USER_MIN = 0x0100;
/** Maximum metadata value size in bytes. */
export const METADATA_VALUE_MAX = 65535;

/** @internal */
export interface MessageSnapshot {
  data: Buffer;
  refCount?: number;
  properties?: Readonly<Record<string, string>>;
  metadata?: Readonly<Map<number, Buffer>>;
}

const EMPTY_PROPERTIES: Readonly<Record<string, string>> = Object.freeze({});
const EMPTY_METADATA: Readonly<Map<number, Buffer>> = Object.freeze(new Map<number, Buffer>());

function normalizeMessageProperties(
  properties?: Readonly<Record<string, string>>
): Readonly<Record<string, string>> {
  if (!properties) {
    return EMPTY_PROPERTIES;
  }
  return Object.isFrozen(properties) ? properties : Object.freeze(properties);
}

export class Message {
  private _buffer!: Buffer;
  private _refCount!: number;
  private _properties!: Readonly<Record<string, string>>;
  private _metadata!: Readonly<Map<number, Buffer>>;

  private constructor(data: BufferLike) {
    this.initialize(Buffer.from(normalizeBufferLike(data, 'data')));
    Object.freeze(this);
  }

  private initialize(
    buffer: Buffer,
    refCount = 1,
    properties?: Readonly<Record<string, string>>,
    metadata?: Readonly<Map<number, Buffer>>
  ): void {
    this._buffer = buffer;
    this._refCount = refCount | 0;
    this._properties = normalizeMessageProperties(properties);
    this._metadata = metadata ?? EMPTY_METADATA;
  }

  static from(buffer: BufferLike | Message): Message {
    return buffer instanceof Message
      ? new Message(buffer._buffer)
      : new Message(buffer);
  }

  static alloc(size: number): Message {
    if (!Number.isSafeInteger(size) || size < 0) {
      throw new RangeError('size must be a non-negative safe integer');
    }
    return Message.fromSnapshot({ data: Buffer.allocUnsafe(size) });
  }

  static allocate(size: number): Message {
    return Message.alloc(size);
  }

  /** @internal */
  static fromSnapshot(snapshot: MessageSnapshot): Message {
    const message = Object.create(Message.prototype) as Message;
    message.initialize(
      snapshot.data,
      snapshot.refCount ?? 1,
      snapshot.properties,
      snapshot.metadata
    );
    return message;
  }

  /** @internal */
  payloadBuffer(): Buffer {
    return this._buffer;
  }

  /** @internal */
  toSnapshot(): MessageSnapshot {
    return {
      data: this._buffer,
      refCount: this._refCount,
      properties: this._properties,
      metadata: this._metadata
    };
  }

  data(): Buffer {
    return this._buffer;
  }

  toBytes(): Buffer {
    return Buffer.from(this._buffer);
  }

  copy(): Message {
    return Message.from(this);
  }

  size(): number {
    return this._buffer.length;
  }

  isEmpty(): boolean {
    return this._buffer.length === 0;
  }

  copyTo(
    destination: Buffer | Uint8Array,
    sourceOffset = 0,
    destinationOffset = 0,
    length = this._buffer.length - sourceOffset
  ): number {
    if (!Buffer.isBuffer(destination) && !(destination instanceof Uint8Array)) {
      throw new TypeError('destination must be a Buffer or Uint8Array');
    }
    if (!Number.isSafeInteger(sourceOffset) || sourceOffset < 0 ||
        !Number.isSafeInteger(destinationOffset) || destinationOffset < 0 ||
        !Number.isSafeInteger(length) || length < 0 ||
        sourceOffset + length > this._buffer.length ||
        destinationOffset + length > destination.byteLength) {
      throw new RangeError('copy range is out of bounds');
    }
    const target = Buffer.isBuffer(destination)
      ? destination
      : Buffer.from(destination.buffer, destination.byteOffset, destination.byteLength);
    return this._buffer.copy(
      target,
      destinationOffset,
      sourceOffset,
      sourceOffset + length
    );
  }

  tryCopyTo(destination: Buffer | Uint8Array): boolean {
    if (destination.byteLength < this._buffer.length) {
      return false;
    }
    this.copyTo(destination);
    return true;
  }

  getString(encoding: BufferEncoding = 'utf8'): string {
    return this._buffer.toString(encoding);
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

  toString(): string {
    return this.getString();
  }
}

export type MessageLike = Message | BufferLike;
