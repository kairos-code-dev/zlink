// SPDX-License-Identifier: MPL-2.0

import type { BufferLike } from '../core/buffer_like';
import { SubmitError, SubmitResult } from '../errors/errors';
import { SendFlags } from '../sockets/socket_constants';
import { Message } from './message';
import { RoutingId } from '../core/routing_id';
import {
  MultipartEnvelope,
  freezeOwnedMessageParts,
} from './envelope';
import { OperationPayload } from './operation_payload';

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

class ReceivedSendOperation implements ReceivedSendOperation, ReceivedSendSubmitOperation {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => boolean;
  private readonly _payload = new OperationPayload<Message | BufferLike, Message>(
    (message) => message instanceof Message ? message : Message.from(message)
  );
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
    return this._invoke(this._payload.consumeParts(), this._flags);
  }
}

class ReceivedReplyOperation implements ReceivedReplyOperation, ReceivedReplySubmitOperation {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => void;
  private readonly _payload = new OperationPayload<Message | BufferLike, Message>(
    (message) => message instanceof Message ? message : Message.from(message)
  );
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
    this._invoke(this._payload.consumeParts(), this._flags);
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

/**
 * A received message envelope: its routing metadata and message parts (from
 * {@link MultipartEnvelope}), plus an optional reply/send context.
 *
 * Owns its parts until closed. Reuse one instance across `recv` calls to avoid
 * a per-receive allocation.
 */
export class Received extends MultipartEnvelope {
  /** The source routing id, or null when the receive path provides none. */
  routingId: RoutingId | null;
  /** The source spot routing id, or null when not from a spot route. */
  spotRid: RoutingId | null;
  /** The request sequence, present when this envelope can be replied to. */
  requestSeq: bigint | null;
  private _replyContext: ReplyContext | null;
  private _sendContext: SendContext | null;

  /** Create an empty reusable envelope for use with `recv`. */
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

  /**
   * Begin a reply to this request: add parts on the returned builder, then
   * submit. Parts are consumed on a successful submit. Throws when the envelope
   * is not replyable (has no request sequence).
   */
  reply(): ReceivedReplyOperation {
    if (!this.requestSeq || !this._replyContext) {
      throw invalidReplyContextError();
    }
    const replyContext = this._replyContext;
    return new ReceivedReplyOperation((parts, flags) => replyContext.reply(parts, flags));
  }

  /**
   * Begin a send addressed to this envelope's source route: add parts, then
   * submit. Parts are consumed on a successful submit. Throws when the envelope
   * carries no send context.
   */
  send(): ReceivedSendOperation {
    if (!this._sendContext) {
      throw invalidSendContextError();
    }
    const sendContext = this._sendContext;
    return new ReceivedSendOperation((parts, flags) => sendContext.send(parts, flags));
  }
}
