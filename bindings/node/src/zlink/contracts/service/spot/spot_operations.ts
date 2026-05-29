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

export type ActorJoinHandler = (result: ActorJoinResult, parts: Message[]) => void;
export type ActorJoinEntrySpotHandler = (result: ActorJoinEntrySpotResult) => void;
export type ActorLookupHandler = (result: ActorLookupResult) => void;

export interface ActorJoinOperation {
  message(message: MessageLike): ActorJoinSubmitOperation;
}

export interface ActorJoinSubmitOperation {
  message(message: MessageLike): ActorJoinSubmitOperation;
  timeout(timeoutMs: number): ActorJoinSubmitOperation;
  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation;
  submitAsync(): Promise<{ result: ActorJoinResult; parts: Message[] }>;
  submit(callback: ActorJoinHandler): boolean;
}

export interface ActorJoinCallbackSubmitOperation {
  message(message: MessageLike): ActorJoinCallbackSubmitOperation;
  timeout(timeoutMs: number): ActorJoinCallbackSubmitOperation;
  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation;
  submit(callback: ActorJoinHandler): boolean;
}

export interface ActorJoinEntrySpotOperation {
  timeout(timeoutMs: number): ActorJoinEntrySpotOperation;
  submitAsync(): Promise<ActorJoinEntrySpotResult>;
  submit(callback: ActorJoinEntrySpotHandler): boolean;
}

export interface ActorJoinReplyOperation {
  message(message: MessageLike): ActorJoinReplyOperation;
  submit(): void;
}

export interface ActorLeaveOperation {
  timeout(timeoutMs: number): ActorLeaveOperation;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorDestroyOperation {
  timeout(timeoutMs: number): ActorDestroyOperation;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorLookupOperation {
  timeout(timeoutMs: number): ActorLookupOperation;
  submitAsync(): Promise<ActorLookupResult>;
  submit(callback: ActorLookupHandler): boolean;
}

export interface ActorBindOperation {
  timeout(timeoutMs: number): ActorBindOperation;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorUnbindOperation {
  timeout(timeoutMs: number): ActorUnbindOperation;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}
