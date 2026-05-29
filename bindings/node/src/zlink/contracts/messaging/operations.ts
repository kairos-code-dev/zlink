// SPDX-License-Identifier: MPL-2.0

import type { RequestResult } from '../errors/errors';
import type { SendFlags } from '../sockets/socket_constants';
import type { Message, MessageLike } from './message';

export type RequestCallback = (result: RequestResult, parts: readonly Message[]) => void;
export type ReplyHandler = (result: RequestResult, parts: Message[]) => void;

export interface SendOperation {
  message(message: MessageLike): SendSubmitOperation;
}

export interface SendSubmitOperation {
  message(message: MessageLike): SendSubmitOperation;
  flags(flags: SendFlags): SendSubmitOperation;
  submit(): boolean;
}

export interface RequestOperation {
  message(message: MessageLike): RequestSubmitOperation;
}

export interface RequestSubmitOperation {
  message(message: MessageLike): RequestSubmitOperation;
  timeout(timeoutMs: number): RequestSubmitOperation;
  flags(flags: SendFlags): RequestCallbackSubmitOperation;
  submitAsync(): Promise<Message[]>;
  submit(callback: RequestCallback): boolean;
}

export interface RequestCallbackSubmitOperation {
  message(message: MessageLike): RequestCallbackSubmitOperation;
  timeout(timeoutMs: number): RequestCallbackSubmitOperation;
  flags(flags: SendFlags): RequestCallbackSubmitOperation;
  submit(callback: RequestCallback): boolean;
}

export interface ReplyOperation {
  message(message: MessageLike): ReplySubmitOperation;
}

export interface ReplySubmitOperation {
  message(message: MessageLike): ReplySubmitOperation;
  flags(flags: SendFlags): ReplySubmitOperation;
  submit(): void;
}
