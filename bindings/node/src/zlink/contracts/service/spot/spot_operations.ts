// SPDX-License-Identifier: MPL-2.0

import type { Message, MessageLike } from '../../messaging';
import type { SendFlags } from '../../sockets/socket_constants';
import type { ReplyHandler } from '../../sockets/socket_operations';
export type {
  ReplyHandler,
  ReplyOp,
  ReplySubmitOp,
  RequestCallback,
  RequestCallbackSubmitOp,
  RequestOp,
  RequestSubmitOp,
  SendOp,
  SendSubmitOp,
} from '../../sockets/socket_operations';
import type {
  ActorJoinEntrySpotResult,
  ActorJoinResult,
  ActorLookupResult,
} from './spot_models';

export type ActorJoinHandler = (result: ActorJoinResult, parts: Message[]) => void;
export type ActorJoinEntrySpotHandler = (result: ActorJoinEntrySpotResult) => void;
export type ActorLookupHandler = (result: ActorLookupResult) => void;

export interface ActorJoinOp {
  message(message: MessageLike): ActorJoinSubmitOp;
}

export interface ActorJoinSubmitOp {
  message(message: MessageLike): ActorJoinSubmitOp;
  timeout(timeoutMs: number): ActorJoinSubmitOp;
  flags(flags: SendFlags): ActorJoinCallbackSubmitOp;
  submitAsync(): Promise<{ result: ActorJoinResult; parts: Message[] }>;
  submit(callback: ActorJoinHandler): boolean;
}

export interface ActorJoinCallbackSubmitOp {
  message(message: MessageLike): ActorJoinCallbackSubmitOp;
  timeout(timeoutMs: number): ActorJoinCallbackSubmitOp;
  flags(flags: SendFlags): ActorJoinCallbackSubmitOp;
  submit(callback: ActorJoinHandler): boolean;
}

export interface ActorJoinEntrySpotOp {
  timeout(timeoutMs: number): ActorJoinEntrySpotOp;
  submitAsync(): Promise<ActorJoinEntrySpotResult>;
  submit(callback: ActorJoinEntrySpotHandler): boolean;
}

export interface ActorJoinReplyOp {
  message(message: MessageLike): ActorJoinReplyOp;
  submit(): void;
}

export interface ActorLeaveOp {
  timeout(timeoutMs: number): ActorLeaveOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorDestroyOp {
  timeout(timeoutMs: number): ActorDestroyOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorLookupOp {
  timeout(timeoutMs: number): ActorLookupOp;
  submitAsync(): Promise<ActorLookupResult>;
  submit(callback: ActorLookupHandler): boolean;
}

export interface ActorBindOp {
  timeout(timeoutMs: number): ActorBindOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorUnbindOp {
  timeout(timeoutMs: number): ActorUnbindOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}
