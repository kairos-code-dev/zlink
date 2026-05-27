// SPDX-License-Identifier: MPL-2.0

import { normalizeBufferLike } from '../core/buffer_like';
import type { BufferLike } from '../core/buffer_like';
import { ConfigError, ConfigResult, RecvError, RecvResult, SubmitError, SubmitResult } from '../errors/errors';
import { SendFlags } from '../sockets/socket_constants';

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

/**
 * Minimal interfaces used by Received.send()/reply() to return operation
 * builders. Defined locally to avoid a dependency cycle with canonical.ts.
 */
interface ReceivedSendOp {
  message(message: Message | BufferLike): ReceivedSendSubmitOp;
}
interface ReceivedSendSubmitOp {
  message(message: Message | BufferLike): ReceivedSendSubmitOp;
  flags(flags: SendFlags): ReceivedSendSubmitOp;
  submit(): boolean;
}
interface ReceivedReplyOp {
  message(message: Message | BufferLike): ReceivedReplySubmitOp;
}
interface ReceivedReplySubmitOp {
  message(message: Message | BufferLike): ReceivedReplySubmitOp;
  flags(flags: SendFlags): ReceivedReplySubmitOp;
  submit(): void;
}

class ReceivedOpPayload {
  private readonly _parts: Message[] = [];
  private _submitted = false;

  append(message: Message | BufferLike): void {
    this.ensureOpen();
    this._parts.push(asMessage(message));
  }

  ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  consume(): readonly Message[] {
    this.ensureOpen();
    if (this._parts.length === 0) {
      throw new TypeError('operation requires at least one message');
    }
    this._submitted = true;
    return this._parts;
  }
}

function asMessage(value: Message | BufferLike): Message {
  return value instanceof Message ? value : Message.from(value);
}

class ReceivedSendOperation implements ReceivedSendOp, ReceivedSendSubmitOp {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => boolean;
  private readonly _payload = new ReceivedOpPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: (parts: readonly Message[], flags: SendFlags) => boolean) {
    this._invoke = invoke;
  }

  message(message: Message | BufferLike): ReceivedSendSubmitOp {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReceivedSendSubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    return this._invoke(this._payload.consume(), this._flags);
  }
}

class ReceivedReplyOperation implements ReceivedReplyOp, ReceivedReplySubmitOp {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => void;
  private readonly _payload = new ReceivedOpPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: (parts: readonly Message[], flags: SendFlags) => void) {
    this._invoke = invoke;
  }

  message(message: Message | BufferLike): ReceivedReplySubmitOp {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReceivedReplySubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume(), this._flags);
  }
}

function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

function freezeOwnedMessageParts(parts: Message[]): Message[] {
  return Object.freeze(parts) as Message[];
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

function normalizeRoutingIdHex(value: string): Buffer {
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

function normalizeRoutingIdValue(value: string | Buffer | Uint8Array | number): Buffer {
  if (typeof value === 'string') {
    return normalizeRoutingIdBytes(Buffer.from(value, 'utf8'), 'value');
  }
  if (typeof value === 'number') {
    if (!Number.isInteger(value) || value < 0 || value > 0xFFFF_FFFF) {
      throw new RangeError('routing id uint32 value must be in range 0..4294967295');
    }
    const buffer = Buffer.allocUnsafe(4);
    buffer.writeUInt32BE(value >>> 0, 0);
    return buffer;
  }
  return normalizeRoutingIdBytes(value, 'value');
}

function tryPrintableUtf8(bytes: Buffer): string | null {
  const text = bytes.toString('utf8');
  if (!Buffer.from(text, 'utf8').equals(bytes)) {
    return null;
  }
  return /[\u0000-\u001f\u007f-\u009f]/u.test(text) ? null : text;
}

function uuidString(bytes: Buffer): string {
  const hex = bytes.toString('hex');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

export class Message {
  private _buffer!: Buffer;
  private _refCount!: number;
  private _properties!: Readonly<Record<string, string>>;
  private _metadata!: Readonly<Map<number, Buffer>>;

  /** @throws {ConfigError} */
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

export class RoutingId {
  private readonly _bytes: Buffer;

  private constructor(token: symbol, bytes: Buffer) {
    if (token !== DOMAIN_CREATE_TOKEN) {
      throw new TypeError('RoutingId values are created with RoutingId.from() or RoutingId.fromHex()');
    }
    this._bytes = bytes;
    Object.freeze(this);
  }

  static from(value: string | Buffer | Uint8Array | number): RoutingId {
    return new RoutingId(DOMAIN_CREATE_TOKEN, normalizeRoutingIdValue(value));
  }

  static fromHex(value: string): RoutingId {
    return new RoutingId(DOMAIN_CREATE_TOKEN, normalizeRoutingIdHex(value));
  }

  /** @deprecated Use RoutingId.from(value). */
  static fromBytes(bytes: Buffer | Uint8Array): RoutingId {
    return RoutingId.from(bytes);
  }

  /** @deprecated Use RoutingId.from(value) for user strings or RoutingId.fromHex(value) for hex. */
  static fromString(value: string): RoutingId {
    return RoutingId.from(value);
  }

  toBytes(): Buffer {
    return Buffer.from(this._bytes);
  }

  /** @internal */
  borrowedBytes(): Buffer {
    return this._bytes;
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
    const utf8 = tryPrintableUtf8(this._bytes);
    if (utf8 !== null) {
      return utf8;
    }
    if (this._bytes.length === 4) {
      return this._bytes.readUInt32BE(0).toString(10);
    }
    if (this._bytes.length === 16) {
      return uuidString(this._bytes);
    }
    return `hex:${this.toHex()}`;
  }
}

class MultipartEnvelope {
  parts: Message[];

  constructor(parts: readonly Message[]) {
    this.parts = freezeMessageParts(parts);
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

  protected replaceParts(parts: readonly Message[]): void {
    this.close();
    this.parts = freezeMessageParts(parts);
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
      this.parts = freezeMessageParts([]);
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
    this.parts = freezeMessageParts(parts ?? []);
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
    source.parts = freezeMessageParts([]);
    source.routingId = null;
    source.spotRid = null;
    source.requestSeq = null;
    source._replyContext = null;
    source._sendContext = null;
  }

  /** @internal */
  _replace(
    parts: Message[],
    routingId: RoutingId | null = null,
    requestSeq: bigint | null = null,
    spotRid: RoutingId | null = null,
    replyContext: ReplyContext | null = null,
    sendContext: SendContext | null = null
  ): void {
    for (const p of this.parts) {
      try { p.close(); } catch { /* swallow */ }
    }
    this.parts = freezeOwnedMessageParts(parts);
    this.routingId = routingId;
    this.spotRid = spotRid;
    this.requestSeq = requestSeq;
    this._replyContext = replyContext;
    this._sendContext = sendContext;
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

  reply(): ReceivedReplyOp {
    if (!this.requestSeq || !this._replyContext) {
      throw invalidReplyContextError();
    }
    const replyContext = this._replyContext;
    return new ReceivedReplyOperation((parts, flags) => replyContext.reply(parts, flags));
  }

  send(): ReceivedSendOp {
    if (!this._sendContext) {
      throw invalidSendContextError();
    }
    const sendContext = this._sendContext;
    return new ReceivedSendOperation((parts, flags) => sendContext.send(parts, flags));
  }

  close(): void {
    for (const part of this.parts) {
      part.close();
    }
  }
}

export class TopicMessage extends MultipartEnvelope {
  routingId: RoutingId | null;
  topic: string;

  constructor();
  constructor(
    token: symbol,
    topic: string,
    parts: readonly Message[],
    routingId?: RoutingId | null
  );
  constructor(
    token?: symbol,
    topic: string = '',
    parts: readonly Message[] = [],
    routingId: RoutingId | null = null
  ) {
    if (token === undefined) {
      super([]);
      this.routingId = null;
      this.topic = '';
      return;
    }
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

  /** @internal */
  adoptFrom(source: TopicMessage): void {
    this.replaceParts(source.parts);
    source.parts = [];
    this.routingId = source.routingId;
    this.topic = source.topic;
    source.routingId = null;
    source.topic = '';
  }

  /** @internal */
  _replace(
    topic: string,
    parts: Message[],
    routingId: RoutingId | null = null
  ): void {
    for (const part of this.parts) {
      try { part.close(); } catch { /* swallow */ }
    }
    this.parts = freezeOwnedMessageParts(parts);
    this.routingId = routingId;
    this.topic = topic;
  }
}

export class SubscriptionEvent {
  routingId: RoutingId | null;
  topic: string;
  subscribed: boolean;

  constructor();
  constructor(
    token: symbol,
    topic: string,
    subscribed: boolean,
    routingId?: RoutingId | null
  );
  constructor(
    token?: symbol,
    topic: string = '',
    subscribed: boolean = false,
    routingId: RoutingId | null = null
  ) {
    if (token === undefined) {
      this.routingId = null;
      this.topic = '';
      this.subscribed = false;
      return;
    }
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

  /** @internal */
  adoptFrom(source: SubscriptionEvent): void {
    this.routingId = source.routingId;
    this.topic = source.topic;
    this.subscribed = source.subscribed;
  }
}

export { TopicMessage as Subscribed };

export type MessageLike = Message | BufferLike;
