// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from './buffer_like';
import type { BufferLike } from './buffer_like';
import { ConfigError, ConfigResult, RecvError, RecvResult, SubmitError, SubmitResult } from './errors';
import { SendFlags } from './socket/constants';

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
const ROUTING_ID_MAX_LENGTH = 255;
const UTF8_DECODER = new TextDecoder('utf-8', { fatal: true });

interface ReplyContext {
  reply(parts: readonly Message[], flags: SendFlags): void;
}

function invalidMultipartError(partsLength: number): RecvError {
  return new RecvError(
    RecvResult.NotSupported,
    0,
    `expected exactly 1 part but received ${partsLength}`
  );
}

function missingPartError(): RecvError {
  return new RecvError(RecvResult.NotSupported, 0, 'message has no parts');
}

function invalidReplyContextError(): SubmitError {
  return new SubmitError(
    SubmitResult.InvalidState,
    0,
    'reply is only valid for request-reply receive contexts'
  );
}

function normalizeRoutingIdBytes(bytes: Buffer | Uint8Array, name: string): Buffer {
  if (!Buffer.isBuffer(bytes) && !(bytes instanceof Uint8Array)) {
    throw new TypeError(`${name} must be a Buffer or Uint8Array`);
  }
  const normalized = Buffer.from(bytes);
  if (normalized.length > ROUTING_ID_MAX_LENGTH) {
    throw new ConfigError(
      ConfigResult.InvalidArgument,
      0,
      `${name} must be 0..${ROUTING_ID_MAX_LENGTH} bytes`
    );
  }
  return normalized;
}

export class Message {
  private _buffer!: Buffer;
  private _refCount!: number;
  private _properties!: Readonly<Record<string, string>>;
  private _metadata!: Readonly<Map<number, Buffer>>;

  /** @throws {ConfigError} */
  constructor(data: BufferLike) {
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

  static from(buffer: BufferLike): Message {
    return new Message(buffer);
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
    Object.freeze(message);
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

  size(): number {
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

export class RoutingId {
  private readonly _bytes: Buffer;

  private constructor(bytes: Buffer) {
    this._bytes = bytes;
    Object.freeze(this);
  }

  static fromBytes(bytes: Buffer | Uint8Array): RoutingId {
    return new RoutingId(normalizeRoutingIdBytes(bytes, 'bytes'));
  }

  static fromU32(value: number): RoutingId {
    if (!Number.isInteger(value) || value < 0 || value > 0xFFFF_FFFF) {
      throw new RangeError('value must be a uint32');
    }
    const raw = Buffer.allocUnsafe(4);
    raw.writeUInt32BE(value >>> 0, 0);
    return new RoutingId(raw);
  }

  static fromUInt32(value: number): RoutingId {
    return RoutingId.fromU32(value);
  }

  static fromText(value: string): RoutingId {
    if (typeof value !== 'string') {
      throw new TypeError('value must be a string');
    }
    return new RoutingId(normalizeRoutingIdBytes(Buffer.from(value, 'utf8'), 'value'));
  }

  static fromString(value: string): RoutingId {
    return RoutingId.fromText(value);
  }

  toBytes(): Buffer {
    return Buffer.from(this._bytes);
  }

  toU32(): number {
    if (this._bytes.length !== 4) {
      throw new RangeError('routing id is not a uint32-sized STREAM routing id');
    }
    return this._bytes.readUInt32BE(0);
  }

  toUInt32(): number {
    return this.toU32();
  }

  toText(): string {
    return UTF8_DECODER.decode(this._bytes);
  }

  toPublicString(): string {
    return this.toText();
  }

  get size(): number {
    return this._bytes.length;
  }

  equals(other: RoutingId): boolean {
    return other instanceof RoutingId && this._bytes.equals(other._bytes);
  }

  toHex(): string {
    return this._bytes.toString('hex');
  }

  toString(): string {
    return this.toHex();
  }
}

class MultipartEnvelope {
  readonly parts: Message[];

  constructor(parts: readonly Message[]) {
    this.parts = Object.freeze(parts.slice()) as Message[];
  }

  isSinglePart(): boolean {
    return this.parts.length === 1;
  }

  firstPart(): Message {
    if (this.parts.length === 0) {
      throw missingPartError();
    }
    return this.parts[0];
  }

  singlePartOrThrow(): Message {
    if (!this.isSinglePart()) {
      throw invalidMultipartError(this.parts.length);
    }
    return this.parts[0];
  }

  toBytesList(): Buffer[] {
    return this.parts.map((part) => part.data());
  }

  close(): void {
    for (const part of this.parts) {
      part.close();
    }
  }
}

export class Received {
  readonly parts: Message[];
  readonly routingId: RoutingId | null;
  readonly spotRid: RoutingId | null;
  readonly requestSeq: bigint | null;
  private readonly _replyContext: ReplyContext | null;

  constructor(
    parts: readonly Message[],
    routingId: RoutingId | null = null,
    requestSeq: bigint | null = null,
    spotRid: RoutingId | null = null,
    replyContext: ReplyContext | null = null
  ) {
    this.parts = Object.freeze(parts.slice()) as Message[];
    this.routingId = routingId;
    this.spotRid = spotRid;
    this.requestSeq = requestSeq;
    this._replyContext = replyContext;
  }

  isSinglePart(): boolean {
    return this.parts.length === 1;
  }

  firstPart(): Message {
    if (this.parts.length === 0) {
      throw missingPartError();
    }
    return this.parts[0];
  }

  singlePartOrThrow(): Message {
    if (!this.isSinglePart()) {
      throw invalidMultipartError(this.parts.length);
    }
    return this.parts[0];
  }

  toBytesList(): Buffer[] {
    return this.parts.map((part) => part.data());
  }

  reply(partOrParts: Message | readonly Message[], flags: SendFlags = SendFlags.None): void {
    if (!this.requestSeq || !this._replyContext) {
      throw invalidReplyContextError();
    }
    const parts = Array.isArray(partOrParts) ? partOrParts : [partOrParts];
    this._replyContext.reply(parts, flags);
  }

  close(): void {
    for (const part of this.parts) {
      part.close();
    }
  }
}

export class TopicMessage extends MultipartEnvelope {
  readonly routingId: RoutingId | null;
  readonly serviceName: string | null;
  readonly topic: string;

  constructor(
    topic: string,
    parts: readonly Message[],
    routingId: RoutingId | null = null,
    serviceName: string | null = null
  ) {
    super(parts);
    this.routingId = routingId;
    this.serviceName = serviceName;
    this.topic = topic;
  }
}

export class SubscriptionEvent {
  readonly routingId: RoutingId | null;
  readonly serviceName: string | null;
  readonly topic: string;
  readonly subscribed: boolean;

  constructor(
    topic: string,
    subscribed: boolean,
    routingId: RoutingId | null = null,
    serviceName: string | null = null
  ) {
    this.routingId = routingId;
    this.serviceName = serviceName;
    this.topic = topic;
    this.subscribed = subscribed === true;
  }
}

export { TopicMessage as Subscribed };

export type MessageLike = Message | BufferLike;
