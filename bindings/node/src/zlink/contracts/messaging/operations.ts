// SPDX-License-Identifier: MPL-2.0

import type { RequestResult } from '../errors/errors';
import type { SendFlags } from '../sockets/socket_constants';
import type { Message, MessageLike } from './message';

/** Invoked with a request result and its reply parts; the callback owns the parts and must close them. */
export type RequestCallback = (result: RequestResult, parts: readonly Message[]) => void;
/** Invoked with a request result and its reply parts; the callback owns the parts. */
export type ReplyHandler = (result: RequestResult, parts: Message[]) => void;

/**
 * Builds a multipart send: add parts, then submit.
 *
 * Submitting consumes the added {@link Message} parts: on a successful submit
 * each part's payload is moved into the transport and the managed message is
 * left empty, so a part must not be reused after a successful submit. The
 * request and reply builders share this same consume-on-submit ownership model.
 */
export interface SendOperation {
  /** Add the first message part; it is consumed on a successful submit. */
  message(message: MessageLike): SendSubmitOperation;
}

/** Accepts further parts, flags, and the terminal submit of a send. */
export interface SendSubmitOperation {
  /** Add another message part; it is consumed on a successful submit. */
  message(message: MessageLike): SendSubmitOperation;
  /** Set the send flags applied at submit time, replacing any previous flags. */
  flags(flags: SendFlags): SendSubmitOperation;
  /**
   * Submit the accumulated parts. Return true when queued, and false only when
   * `SendFlags.DontWait` is set and the send would have blocked (back-pressure).
   */
  submit(): boolean;
}

/** Builds a request: add the request parts, then submit and await a reply. */
export interface RequestOperation {
  /** Add the first request part; it is consumed on a successful submit. */
  message(message: MessageLike): RequestSubmitOperation;
}

/** Accepts further parts, timeout, flags, and the terminal submit of a request. */
export interface RequestSubmitOperation {
  /** Add another request part; it is consumed on a successful submit. */
  message(message: MessageLike): RequestSubmitOperation;
  /** Set how long the request waits for a reply before timing out. */
  timeout(timeoutMs: number): RequestSubmitOperation;
  /** Set the send flags and narrow the builder to callback submission. */
  flags(flags: SendFlags): RequestCallbackSubmitOperation;
  /** Submit the request and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit the request; the result and reply parts are delivered to `callback`. Returns false under DontWait back-pressure. */
  submit(callback: RequestCallback): boolean;
}

/** Callback-submission stage of a request (reached after setting flags). */
export interface RequestCallbackSubmitOperation {
  /** Add another request part; it is consumed on a successful submit. */
  message(message: MessageLike): RequestCallbackSubmitOperation;
  /** Set how long the request waits for a reply before timing out. */
  timeout(timeoutMs: number): RequestCallbackSubmitOperation;
  /** Set the send flags applied at submit time. */
  flags(flags: SendFlags): RequestCallbackSubmitOperation;
  /** Submit the request; the result and reply parts are delivered to `callback`. */
  submit(callback: RequestCallback): boolean;
}

/** Builds a reply to a received request: add the reply parts, then submit. */
export interface ReplyOperation {
  /** Add the first reply part; it is consumed on a successful submit. */
  message(message: MessageLike): ReplySubmitOperation;
}

/** Accepts further parts, flags, and the terminal submit of a reply. */
export interface ReplySubmitOperation {
  /** Add another reply part; it is consumed on a successful submit. */
  message(message: MessageLike): ReplySubmitOperation;
  /** Set the send flags applied at submit time. */
  flags(flags: SendFlags): ReplySubmitOperation;
  /** Submit the accumulated reply parts. */
  submit(): void;
}
