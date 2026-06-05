// SPDX-License-Identifier: MPL-2.0

import { RecvError, RecvResult, SubmitError, SubmitResult } from '../errors/errors';
import { Message } from './message';
import type { ReplyOperation, SendOperation } from './operations';
import { RoutingId } from '../core/routing_id';

interface ReplyContext {
  beginReply(): ReplyOperation;
}

interface SendContext {
  beginSend(): SendOperation;
}

function freezeMessageParts(parts: readonly Message[]): Message[] {
  return Object.freeze(parts.slice()) as Message[];
}

function invalidMultipartError(partsLength: number): RecvError {
  const error = new RecvError(RecvResult.NotSupported, 0);
  error.message = `expected exactly 1 part but received ${partsLength}`;
  return error;
}

function missingPartError(): RecvError {
  const error = new RecvError(RecvResult.NotSupported, 0);
  error.message = 'message has no parts';
  return error;
}

function invalidReplyContextError(): SubmitError {
  const error = new SubmitError(SubmitResult.InvalidState, 0);
  error.message = 'reply is only valid for request-reply receive contexts';
  return error;
}

function invalidSendContextError(): SubmitError {
  const error = new SubmitError(SubmitResult.InvalidState, 0);
  error.message = 'send is only valid for received routed message contexts';
  return error;
}

/**
 * A received message envelope: its routing metadata and message parts, plus an
 * optional reply/send context.
 *
 * Owns its parts until closed. Reuse one instance across `recv` calls to avoid
 * a per-receive allocation.
 */
export class Received {
  /** The message parts, owned by this envelope. */
  parts: Message[];
  /** The source routing id, or null when the receive path provides none. */
  routingId: RoutingId | null;
  /** The source spot routing id, or null when not from a spot route. */
  spotRid: RoutingId | null;
  /** The request sequence, present when this envelope can be replied to. */
  requestSeq: bigint | null;
  private _replyContext: ReplyContext | null;
  private _sendContext: SendContext | null;

  /** Create an empty reusable envelope for use with `recv`. */
  constructor() {
    this.parts = freezeMessageParts([]);
    this.routingId = null;
    this.spotRid = null;
    this.requestSeq = null;
    this._replyContext = null;
    this._sendContext = null;
  }

  /** Return true when the envelope holds exactly one part. */
  isSinglePart(): boolean {
    return this.parts.length === 1;
  }

  /** Return the first part without transferring ownership; throws when the envelope has no parts. */
  firstPart(): Message {
    if (this.parts.length === 0) {
      throw missingPartError();
    }
    return this.parts[0];
  }

  /** Return the only part; throws unless the envelope holds exactly one part. */
  singlePartOrThrow(): Message {
    if (!this.isSinglePart()) {
      throw invalidMultipartError(this.parts.length);
    }
    return this.parts[0];
  }

  /** Close every part, releasing their storage. */
  close(): void {
    for (const part of this.parts) {
      part.close();
    }
  }

  /**
   * Begin a reply to this request: add parts on the returned builder, then
   * submit. Parts are consumed on a successful submit. Throws when the envelope
   * is not replyable (has no request sequence).
   */
  reply(): ReplyOperation {
    if (!this.requestSeq || !this._replyContext) {
      throw invalidReplyContextError();
    }
    return this._replyContext.beginReply();
  }

  /**
   * Begin a send addressed to this envelope's source route: add parts, then
   * submit. Parts are consumed on a successful submit. Throws when the envelope
   * carries no send context.
   */
  send(): SendOperation {
    if (!this._sendContext) {
      throw invalidSendContextError();
    }
    return this._sendContext.beginSend();
  }
}
