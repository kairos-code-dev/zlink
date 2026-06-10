// SPDX-License-Identifier: MPL-2.0

import type { Message, MessageLike } from '../../messaging';
import type { SendFlags } from '../../sockets/socket_constants';
import type { ReplyHandler } from '../../messaging/operations';
export type {
  ReplyHandler,
  ReplyOperation,
  ReplySubmitOperation,
  RequestCallback,
  RequestCallbackSubmitOperation,
  RequestOperation,
  RequestSubmitOperation,
  SendOperation,
  SendSubmitOperation,
} from '../../messaging/operations';
import type {
  ActorJoinEntrySpotResult,
  ActorJoinResult,
  ActorLookupResult,
} from './spot_models';

/** Invoked with the result of an actor join and its reply parts, which the callback owns. */
export type ActorJoinHandler = (result: ActorJoinResult, parts: Message[]) => void;
/** Invoked with the result of an actor join routed through an entry spot. */
export type ActorJoinEntrySpotHandler = (result: ActorJoinEntrySpotResult) => void;
/** Invoked with the result of an actor lookup. */
export type ActorLookupHandler = (result: ActorLookupResult) => void;

/** An actor-join builder: add parts, then submit and await the result. Parts are consumed on a successful submit. */
export interface ActorJoinOperation {
  /** Add the first message part; it is consumed on a successful submit. */
  message(message: MessageLike): ActorJoinSubmitOperation;
}

/** Accepts further parts, timeout, flags, and the terminal submit of an actor join. */
export interface ActorJoinSubmitOperation {
  /** Add another message part; it is consumed on a successful submit. */
  message(message: MessageLike): ActorJoinSubmitOperation;
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorJoinSubmitOperation;
  /** Set the send flags and narrow the builder to callback submission. */
  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation;
  /** Submit and return the result and reply parts, which the caller owns. */
  submit(): Promise<{ result: ActorJoinResult; parts: Message[] }>;
  /** Submit; the result and reply parts are delivered to `callback`. */
  submit(callback: ActorJoinHandler): boolean;
}

/** Callback-submission stage of an actor join. */
export interface ActorJoinCallbackSubmitOperation {
  /** Add another message part; it is consumed on a successful submit. */
  message(message: MessageLike): ActorJoinCallbackSubmitOperation;
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorJoinCallbackSubmitOperation;
  /** Set the send flags applied at submit time. */
  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation;
  /** Submit; the result and reply parts are delivered to `callback`. */
  submit(callback: ActorJoinHandler): boolean;
}

/** A builder for joining an actor through an entry spot. */
export interface ActorJoinEntrySpotOperation {
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorJoinEntrySpotOperation;
  /** Submit and return the result. */
  submit(): Promise<ActorJoinEntrySpotResult>;
  /** Submit; the result is delivered to `callback`. */
  submit(callback: ActorJoinEntrySpotHandler): boolean;
}

/** A builder for replying to an actor-join request; a zero-part reply is allowed. */
export interface ActorJoinReplyOperation {
  /** Add a reply part; it is consumed on a successful submit. */
  message(message: MessageLike): ActorJoinReplyOperation;
  /** Submit the actor-join reply. */
  submit(): void;
}

/** A builder for an actor leave. */
export interface ActorLeaveOperation {
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorLeaveOperation;
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}

/** A builder for destroying an actor. */
export interface ActorDestroyOperation {
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorDestroyOperation;
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}

/** A builder for looking up a remote actor's reference. */
export interface ActorLookupOperation {
  /** Set how long the lookup waits for completion before timing out. */
  timeout(timeoutMs: number): ActorLookupOperation;
  /** Submit and return the lookup result. */
  submit(): Promise<ActorLookupResult>;
  /** Submit; the result is delivered to `callback`. */
  submit(callback: ActorLookupHandler): boolean;
}

/** A builder for binding an actor to a session. */
export interface ActorBindOperation {
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorBindOperation;
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}

/** A builder for removing an actor's session binding. */
export interface ActorUnbindOperation {
  /** Set how long the operation waits for completion before timing out. */
  timeout(timeoutMs: number): ActorUnbindOperation;
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}
