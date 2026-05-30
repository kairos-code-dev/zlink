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

/**
 * A message payload. Sending a message consumes it; `close` releases its
 * storage.
 */
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

  /**
   * Create a message holding an independent copy of `buffer` (a buffer-like
   * value or another {@link Message}); the source may be freely reused
   * afterward.
   */
  static from(buffer: BufferLike | Message): Message {
    return buffer instanceof Message
      ? new Message(buffer._buffer)
      : new Message(buffer);
  }

  /** Allocate a message with `size` bytes of writable payload storage. */
  static alloc(size: number): Message {
    if (!Number.isSafeInteger(size) || size < 0) {
      throw new RangeError('size must be a non-negative safe integer');
    }
    return Message.fromSnapshot({ data: Buffer.allocUnsafe(size) });
  }

  /** Alias for {@link Message.alloc}. */
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

  /** Return the payload as a Buffer backed by this message's storage. */
  data(): Buffer {
    return this._buffer;
  }

  /** Return a new Buffer copying the payload. */
  toBytes(): Buffer {
    return Buffer.from(this._buffer);
  }

  /** Return a new message holding an independent copy of this payload. */
  copy(): Message {
    return Message.from(this);
  }

  /** Return the payload size in bytes. */
  size(): number {
    return this._buffer.length;
  }

  /** Return true when the payload is empty. */
  isEmpty(): boolean {
    return this._buffer.length === 0;
  }

  /**
   * Copy the payload (or `length` bytes from `sourceOffset`) into `destination`
   * at `destinationOffset`; return the number of bytes written. Throws
   * {@link RangeError} when the range is out of bounds.
   */
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

  /**
   * Copy the payload into `destination` when it fits; return true on success,
   * or false when `destination` is too small.
   */
  tryCopyTo(destination: Buffer | Uint8Array): boolean {
    if (destination.byteLength < this._buffer.length) {
      return false;
    }
    this.copyTo(destination);
    return true;
  }

  /** Decode the payload as text using `encoding`. */
  getString(encoding: BufferEncoding = 'utf8'): string {
    return this._buffer.toString(encoding);
  }

  /** Return the native message property `name`, or null when it is absent. */
  getProperty(name: string): string | null {
    if (typeof name !== 'string') {
      throw new TypeError('property name must be a string');
    }
    return Object.prototype.hasOwnProperty.call(this._properties, name)
      ? this._properties[name]
      : null;
  }

  /** Return the native payload reference count (a diagnostic only). */
  refCount(): number {
    return this._refCount;
  }

  /** Release the payload storage owned by this message. */
  close(): void {}

  /** Return the payload decoded as a UTF-8 string. */
  toString(): string {
    return this.getString();
  }
}

export type MessageLike = Message | BufferLike;
