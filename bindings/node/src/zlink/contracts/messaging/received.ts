// SPDX-License-Identifier: MPL-2.0

import type { BufferLike } from '../core/buffer_like';
import { SubmitError, SubmitResult } from '../errors/errors';
import { SendFlags } from '../sockets/socket_constants';
import { Message } from './message';
import { RoutingId } from '../core/routing_id';
import {
  MultipartEnvelope,
  freezeMessageParts,
  freezeOwnedMessageParts,
} from './envelope';

const RECEIVED_CREATE_TOKEN = Symbol('received.create');

interface ReplyContext {
  reply(parts: readonly Message[], flags: SendFlags): void;
}

interface SendContext {
  send(parts: readonly Message[], flags: SendFlags): boolean;
}

interface ReceivedSendOperation {
  message(message: Message | BufferLike): ReceivedSendSubmitOperation;
}
interface ReceivedSendSubmitOperation {
  message(message: Message | BufferLike): ReceivedSendSubmitOperation;
  flags(flags: SendFlags): ReceivedSendSubmitOperation;
  submit(): boolean;
}
interface ReceivedReplyOperation {
  message(message: Message | BufferLike): ReceivedReplySubmitOperation;
}
interface ReceivedReplySubmitOperation {
  message(message: Message | BufferLike): ReceivedReplySubmitOperation;
  flags(flags: SendFlags): ReceivedReplySubmitOperation;
  submit(): void;
}

class ReceivedOpPayload {
  private readonly _parts: Message[] = [];
  private _submitted = false;

  append(message: Message | BufferLike): void {
    this.ensureOpen();
    this._parts.push(message instanceof Message ? message : Message.from(message));
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

class ReceivedSendOperation implements ReceivedSendOperation, ReceivedSendSubmitOperation {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => boolean;
  private readonly _payload = new ReceivedOpPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: (parts: readonly Message[], flags: SendFlags) => boolean) {
    this._invoke = invoke;
  }

  message(message: Message | BufferLike): ReceivedSendSubmitOperation {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReceivedSendSubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    return this._invoke(this._payload.consume(), this._flags);
  }
}

class ReceivedReplyOperation implements ReceivedReplyOperation, ReceivedReplySubmitOperation {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => void;
  private readonly _payload = new ReceivedOpPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: (parts: readonly Message[], flags: SendFlags) => void) {
    this._invoke = invoke;
  }

  message(message: Message | BufferLike): ReceivedReplySubmitOperation {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReceivedReplySubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume(), this._flags);
  }
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

export class Received extends MultipartEnvelope {
  routingId: RoutingId | null;
  spotRid: RoutingId | null;
  requestSeq: bigint | null;
  private _replyContext: ReplyContext | null;
  private _sendContext: SendContext | null;

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
      super([]);
      this.routingId = null;
      this.spotRid = null;
      this.requestSeq = null;
      this._replyContext = null;
      this._sendContext = null;
      return;
    }
    if (token !== RECEIVED_CREATE_TOKEN) {
      throw new TypeError('Received values are created by recv operations');
    }
    super(parts ?? []);
    this.routingId = routingId;
    this.spotRid = spotRid;
    this.requestSeq = requestSeq;
    this._replyContext = replyContext;
    this._sendContext = sendContext;
  }

  /** @internal */
  _adoptFrom(source: Received): void {
    if (source === this) return;
    for (const p of this.parts) {
      try { p.close(); } catch { /* swallow */ }
    }
    this.parts = source.parts;
    this.routingId = source.routingId;
    this.spotRid = source.spotRid;
    this.requestSeq = source.requestSeq;
    this._replyContext = source._replyContext;
    this._sendContext = source._sendContext;
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
    return new Received(RECEIVED_CREATE_TOKEN, parts, routingId, requestSeq, spotRid, replyContext, sendContext);
  }

  reply(): ReceivedReplyOperation {
    if (!this.requestSeq || !this._replyContext) {
      throw invalidReplyContextError();
    }
    const replyContext = this._replyContext;
    return new ReceivedReplyOperation((parts, flags) => replyContext.reply(parts, flags));
  }

  send(): ReceivedSendOperation {
    if (!this._sendContext) {
      throw invalidSendContextError();
    }
    const sendContext = this._sendContext;
    return new ReceivedSendOperation((parts, flags) => sendContext.send(parts, flags));
  }
}
