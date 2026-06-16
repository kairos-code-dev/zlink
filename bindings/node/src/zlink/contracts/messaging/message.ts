// SPDX-License-Identifier: MPL-2.0

import type { BufferLike } from '../core/buffer_like';

/** Minimum user-defined metadata key. */
export const METADATA_KEY_USER_MIN = 0x0100;
/** Maximum metadata value size in bytes. */
export const METADATA_VALUE_MAX = 65535;

const EMPTY_PROPERTIES: Readonly<Record<string, string>> = Object.freeze({});
function normalizeMessageProperties(
  properties?: Readonly<Record<string, string>>
): Readonly<Record<string, string>> {
  if (!properties) {
    return EMPTY_PROPERTIES;
  }
  return Object.isFrozen(properties) ? properties : Object.freeze(properties);
}

type ObjectMessagePayload = object & {
  data?: () => Buffer | Uint8Array;
  toBytes?: () => Buffer | Uint8Array;
  serializeBinary?: () => Buffer | Uint8Array;
};

function normalizeBufferLike(value: BufferLike | ObjectMessagePayload, label = 'value'): Buffer {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (typeof value === 'string') return Buffer.from(value);
  if (typeof value === 'object' && value !== null) {
    if (typeof value.toBytes === 'function') {
      return normalizeBufferLike(value.toBytes(), `${label}.toBytes()`);
    }
    if (typeof value.serializeBinary === 'function') {
      return normalizeBufferLike(value.serializeBinary(), `${label}.serializeBinary()`);
    }
    if (typeof value.data === 'function') {
      return normalizeBufferLike(value.data(), `${label}.data()`);
    }
    return Buffer.from(JSON.stringify(value), 'utf8');
  }
  throw new TypeError(`${label} must be Buffer, Uint8Array, or string`);
}

/**
 * A message payload. Sending a message consumes it; `close` releases its
 * storage.
 */
export class Message {
  private _buffer!: Buffer;
  private _refCount!: number;
  private _properties!: Readonly<Record<string, string>>;
  private _value?: unknown;
  private _hasValue = false;
  private _packetName?: string;

  private constructor(
    data: BufferLike | ObjectMessagePayload,
    value?: unknown,
    hasValue = value !== undefined,
    packetName?: string
  ) {
    this.initialize(Buffer.from(normalizeBufferLike(data, 'data')));
    if (hasValue) {
      this._value = value;
      this._hasValue = true;
    }
    this._packetName = packetName;
    Object.freeze(this);
  }

  private initialize(
    buffer: Buffer,
    refCount = 1,
    properties?: Readonly<Record<string, string>>
  ): void {
    this._buffer = buffer;
    this._refCount = refCount | 0;
    this._properties = normalizeMessageProperties(properties);
  }

  /**
   * Create a message holding an independent copy of `buffer` (a buffer-like
   * value, object payload, or another {@link Message}); the source may be
   * freely reused afterward. Object payloads are serialized immediately.
   */
  static from(buffer: BufferLike | object | Message): Message {
    if (buffer instanceof Message) {
      return new Message(buffer._buffer, buffer._value, buffer._hasValue, buffer._packetName);
    }
    return isObjectPayload(buffer)
      ? new Message(buffer as ObjectMessagePayload, buffer, true, inferPacketName(buffer))
      : new Message(buffer);
  }

  /** Allocate a message with `size` bytes of writable payload storage. */
  static allocate(size: number): Message {
    if (!Number.isSafeInteger(size) || size < 0) {
      throw new RangeError('size must be a non-negative safe integer');
    }
    return new Message(Buffer.allocUnsafe(size));
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

  /** Return the packet name captured for this message, if known. */
  packetName(): string | undefined {
    return this._packetName;
  }

  /** Return a copy of this message with `packetName` metadata attached. */
  withPacketName(packetName: string): Message {
    if (typeof packetName !== 'string' || packetName.trim().length === 0) {
      throw new TypeError('packetName must be a non-empty string');
    }
    return new Message(this._buffer, this._value, this._hasValue, packetName);
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

  /** Return the object payload used to create this message, or decode JSON bytes. */
  value<T = unknown>(): T {
    if (this._hasValue) {
      return this._value as T;
    }
    return JSON.parse(this.getString()) as T;
  }
}

function isObjectPayload(value: BufferLike | object): value is ObjectMessagePayload {
  return typeof value === 'object' && value !== null && !Buffer.isBuffer(value) && !(value instanceof Uint8Array);
}

function inferPacketName(value: object): string | undefined {
  const name = Object.getPrototypeOf(value)?.constructor?.name;
  if (typeof name !== 'string' || isStructuralPayloadName(name)) {
    return undefined;
  }
  return name;
}

function isStructuralPayloadName(name: string): boolean {
  return [
    'Object',
    'Array',
    'Buffer',
    'Uint8Array',
    'String',
    'Number',
    'Boolean',
    'BigInt',
    'Symbol',
    'Date'
  ].includes(name);
}

export type MessageLike = Message | BufferLike;
