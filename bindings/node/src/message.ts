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
const DOMAIN_CREATE_TOKEN = Symbol('domain-value.create');

interface ReplyContext {
  reply(parts: readonly Message[], flags: SendFlags): void;
}

interface SendContext {
  send(parts: readonly Message[], flags: SendFlags): boolean;
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

function invalidSendContextError(): SubmitError {
  return new SubmitError(
    SubmitResult.InvalidState,
    0,
    'send is only valid for received routed message contexts'
  );
}

function normalizeRoutingIdBytes(bytes: Buffer | Uint8Array, name: string): Buffer {
  if (!Buffer.isBuffer(bytes) && !(bytes instanceof Uint8Array)) {
    throw new TypeError(`${name} must be a Buffer or Uint8Array`);
  }
  const normalized = Buffer.from(bytes);
  if (normalized.length === 0 || normalized.length > ROUTING_ID_MAX_LENGTH) {
    throw new ConfigError(
      ConfigResult.InvalidArgument,
      0,
      `${name} must be 1..${ROUTING_ID_MAX_LENGTH} bytes`
    );
  }
  return normalized;
}

function normalizeRoutingIdString(value: string): Buffer {
  if (typeof value !== 'string') {
    throw new TypeError('value must be a string');
  }
  if (value.length === 0 || value.length % 2 !== 0 || !/^[0-9a-fA-F]+$/.test(value)) {
    throw new ConfigError(
      ConfigResult.InvalidArgument,
      0,
      'value must be a non-empty even-length hex string'
    );
  }
  if (value.length > ROUTING_ID_MAX_LENGTH * 2) {
    throw new ConfigError(
      ConfigResult.InvalidArgument,
      0,
      `value must decode to at most ${ROUTING_ID_MAX_LENGTH} bytes`
    );
  }
  return normalizeRoutingIdBytes(Buffer.from(value, 'hex'), 'value');
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

  refCount(): number {
    return this._refCount;
  }

  close(): void {}
}

export class RoutingId {
  private readonly _bytes: Buffer;

  private constructor(token: symbol, bytes: Buffer) {
    if (token !== DOMAIN_CREATE_TOKEN) {
      throw new TypeError('RoutingId values are created with RoutingId.fromBytes() or RoutingId.fromString()');
    }
    this._bytes = bytes;
    Object.freeze(this);
  }

  static fromBytes(bytes: Buffer | Uint8Array): RoutingId {
    return new RoutingId(DOMAIN_CREATE_TOKEN, normalizeRoutingIdBytes(bytes, 'bytes'));
  }

  static fromString(value: string): RoutingId {
    return new RoutingId(DOMAIN_CREATE_TOKEN, normalizeRoutingIdString(value));
  }

  toBytes(): Buffer {
    return Buffer.from(this._bytes);
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

  close(): void {
    for (const part of this.parts) {
      part.close();
    }
  }
}

export class Received {
  parts: Message[];
  routingId: RoutingId | null;
  spotRid: RoutingId | null;
  requestSeq: bigint | null;
  private _replyContext: ReplyContext | null;
  private _sendContext: SendContext | null;

  /**
   * Create an empty Received for caller-provided storage. Hand the same
   * instance to {@code socket.recv(received, flags)} across calls to avoid
   * the per-recv allocation; the binding overwrites the internal state on
   * each successful receive. See doc/spec/bindings/README.md "Canonical
   * Recv: Caller-Provided Storage".
   */
  constructor();
  constructor(
    token: symbol,
    parts: readonly Message[],
    routingId?: RoutingId | null,
    requestSeq?: bigint | null,
    spotRid?: RoutingId | null,
    replyContext?: ReplyContext | null,
    sendContext?: SendContext | null
  );
  constructor(
    token?: symbol,
    parts?: readonly Message[],
    routingId: RoutingId | null = null,
    requestSeq: bigint | null = null,
    spotRid: RoutingId | null = null,
    replyContext: ReplyContext | null = null,
    sendContext: SendContext | null = null
  ) {
    if (token === undefined && parts === undefined) {
      // Empty caller-provided storage instance.
      this.parts = [];
      this.routingId = null;
      this.spotRid = null;
      this.requestSeq = null;
      this._replyContext = null;
      this._sendContext = null;
      return;
    }
    if (token !== DOMAIN_CREATE_TOKEN) {
      throw new TypeError('Received values are created by recv operations');
    }
    this.parts = (parts ?? []).slice() as Message[];
    this.routingId = routingId;
    this.spotRid = spotRid;
    this.requestSeq = requestSeq;
    this._replyContext = replyContext;
    this._sendContext = sendContext;
  }

  /**
   * Replace this Received's internal state with a fresh recv result.
   * Closes any messages currently owned by this instance before adopting.
   * @internal
   */
  _adoptFrom(source: Received): void {
    if (source === this) return;
    // Close any prior owned parts.
    for (const p of this.parts) {
      try { p.close(); } catch { /* swallow */ }
    }
    this.parts = source.parts;
    this.routingId = source.routingId;
    this.spotRid = source.spotRid;
    this.requestSeq = source.requestSeq;
    this._replyContext = source._replyContext;
    this._sendContext = source._sendContext;
    // Detach source so it does not double-close.
    source.parts = [];
    source.routingId = null;
    source.spotRid = null;
    source.requestSeq = null;
    source._replyContext = null;
    source._sendContext = null;
  }

  /** @internal */
  static create(
    parts: readonly Message[],
    routingId: RoutingId | null = null,
    requestSeq: bigint | null = null,
    spotRid: RoutingId | null = null,
    replyContext: ReplyContext | null = null,
    sendContext: SendContext | null = null
  ): Received {
    return new Received(DOMAIN_CREATE_TOKEN, parts, routingId, requestSeq, spotRid, replyContext, sendContext);
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

  reply(part: Message, flags?: SendFlags): void;
  reply(parts: Message[], flags?: SendFlags): void;
  reply(partOrParts: Message | readonly Message[], flags: SendFlags = SendFlags.None): void {
    if (!this.requestSeq || !this._replyContext) {
      throw invalidReplyContextError();
    }
    const parts = Array.isArray(partOrParts) ? partOrParts : [partOrParts];
    this._replyContext.reply(parts, flags);
  }

  send(part: Message, flags?: SendFlags): boolean;
  send(parts: Message[], flags?: SendFlags): boolean;
  send(partOrParts: Message | readonly Message[], flags: SendFlags = SendFlags.None): boolean {
    if (!this._sendContext) {
      throw invalidSendContextError();
    }
    const parts = Array.isArray(partOrParts) ? partOrParts : [partOrParts];
    return this._sendContext.send(parts, flags);
  }

  close(): void {
    for (const part of this.parts) {
      part.close();
    }
  }
}

export class TopicMessage extends MultipartEnvelope {
  readonly routingId: RoutingId | null;
  readonly topic: string;

  private constructor(
    token: symbol,
    topic: string,
    parts: readonly Message[],
    routingId: RoutingId | null = null
  ) {
    if (token !== DOMAIN_CREATE_TOKEN) {
      throw new TypeError('TopicMessage values are created by subscribe operations');
    }
    super(parts);
    this.routingId = routingId;
    this.topic = topic;
  }

  /** @internal */
  static create(
    topic: string,
    parts: readonly Message[],
    routingId: RoutingId | null = null
  ): TopicMessage {
    return new TopicMessage(DOMAIN_CREATE_TOKEN, topic, parts, routingId);
  }
}

export class SubscriptionEvent {
  readonly routingId: RoutingId | null;
  readonly topic: string;
  readonly subscribed: boolean;

  private constructor(
    token: symbol,
    topic: string,
    subscribed: boolean,
    routingId: RoutingId | null = null
  ) {
    if (token !== DOMAIN_CREATE_TOKEN) {
      throw new TypeError('SubscriptionEvent values are created by subscription event operations');
    }
    this.routingId = routingId;
    this.topic = topic;
    this.subscribed = subscribed === true;
  }

  /** @internal */
  static create(
    topic: string,
    subscribed: boolean,
    routingId: RoutingId | null = null
  ): SubscriptionEvent {
    return new SubscriptionEvent(DOMAIN_CREATE_TOKEN, topic, subscribed, routingId);
  }
}

export { TopicMessage as Subscribed };

export type MessageLike = Message | BufferLike;
