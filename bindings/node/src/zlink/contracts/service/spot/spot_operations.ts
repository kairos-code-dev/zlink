// SPDX-License-Identifier: MPL-2.0

import type { Message } from '../../messaging';
import type { SendFlags } from '../../sockets/socket_constants';
import type { Flaggable, PartBuilder, ReplyHandler, Timeoutable } from '../../messaging/operations';
export type * from '../../messaging/operations';
import type {
  ActorJoinEntrySpotResult,
  ActorJoinResult,
  ActorLookupResult,
} from './spot_models';

/** Invoked with the result of an actor join and its reply parts, which the callback owns. */
export type ActorJoinHandler = (result: ActorJoinResult, parts: Message[]) => void;
/** Invoked with the result of an actor entry-spot join and its reply parts, which the callback owns. */
export type ActorJoinEntrySpotHandler = (result: ActorJoinEntrySpotResult, parts: Message[]) => void;
/** Invoked with the result of an actor lookup. */
export type ActorLookupHandler = (result: ActorLookupResult) => void;

/** An actor-join builder: add parts, then submit and await the result. Parts are consumed on a successful submit. */
export interface ActorJoinOperation extends PartBuilder<ActorJoinSubmitOperation> {}

/** Accepts further parts, timeout, flags, and the terminal submit of an actor join. */
export interface ActorJoinSubmitOperation
  extends PartBuilder<ActorJoinSubmitOperation>, Timeoutable<ActorJoinSubmitOperation> {
  /** Set the send flags and narrow the builder to callback submission. */
  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation;
  /** Submit and return the result and reply parts, which the caller owns. */
  submit(): Promise<{ result: ActorJoinResult; parts: Message[] }>;
  /** Submit; the result and reply parts are delivered to `callback`. */
  submit(callback: ActorJoinHandler): boolean;
}

/** Callback-submission stage of an actor join. */
export interface ActorJoinCallbackSubmitOperation
  extends PartBuilder<ActorJoinCallbackSubmitOperation>,
    Timeoutable<ActorJoinCallbackSubmitOperation>,
    Flaggable<ActorJoinCallbackSubmitOperation> {
  /** Submit; the result and reply parts are delivered to `callback`. */
  submit(callback: ActorJoinHandler): boolean;
}

/** A builder for joining an actor through an entry spot. */
export interface ActorJoinEntrySpotOperation
  extends PartBuilder<ActorJoinEntrySpotOperation>,
    Timeoutable<ActorJoinEntrySpotOperation>,
    Flaggable<ActorJoinEntrySpotOperation> {
  /** Submit and return the result and reply parts, which the caller owns. */
  submit(): Promise<{ result: ActorJoinEntrySpotResult; parts: Message[] }>;
  /** Submit; the result and reply parts are delivered to `callback`. */
  submit(callback: ActorJoinEntrySpotHandler): boolean;
}

/** A builder for replying to an actor-join request; a zero-part reply is allowed. */
export interface ActorJoinReplyOperation extends PartBuilder<ActorJoinReplyOperation> {
  /** Submit the actor-join reply. */
  submit(): void;
}

/** A builder for an actor leave. */
export interface ActorLeaveOperation extends Timeoutable<ActorLeaveOperation> {
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}

/** A builder for destroying an actor. */
export interface ActorDestroyOperation extends Timeoutable<ActorDestroyOperation> {
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}

/** A builder for looking up a remote actor's reference. */
export interface ActorLookupOperation extends Timeoutable<ActorLookupOperation> {
  /** Submit and return the lookup result. */
  submit(): Promise<ActorLookupResult>;
  /** Submit; the result is delivered to `callback`. */
  submit(callback: ActorLookupHandler): boolean;
}

/** A builder for binding an actor to a session. */
export interface ActorBindOperation extends Timeoutable<ActorBindOperation> {
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}

/** A builder for removing an actor's session binding. */
export interface ActorUnbindOperation extends Timeoutable<ActorUnbindOperation> {
  /** Submit and return the reply parts, which the caller owns. */
  submit(): Promise<Message[]>;
  /** Submit; the reply is delivered to `callback`. */
  submit(callback: ReplyHandler): boolean;
}
