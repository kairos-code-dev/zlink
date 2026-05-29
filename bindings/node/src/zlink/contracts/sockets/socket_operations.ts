// SPDX-License-Identifier: MPL-2.0

import type { Message, MessageLike } from '../messaging';
import type { RequestResult } from '../errors/errors';
import type { SendFlags } from './socket_constants';

export type RequestCallback = (result: RequestResult, parts: readonly Message[]) => void;
export type ReplyHandler = (result: RequestResult, parts: Message[]) => void;

export interface SendOp {
  message(message: MessageLike): SendSubmitOp;
}

export interface SendSubmitOp {
  message(message: MessageLike): SendSubmitOp;
  flags(flags: SendFlags): SendSubmitOp;
  submit(): boolean;
}

export interface RequestOp {
  message(message: MessageLike): RequestSubmitOp;
}

export interface RequestSubmitOp {
  message(message: MessageLike): RequestSubmitOp;
  timeout(timeoutMs: number): RequestSubmitOp;
  flags(flags: SendFlags): RequestCallbackSubmitOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: RequestCallback): boolean;
}

export interface RequestCallbackSubmitOp {
  message(message: MessageLike): RequestCallbackSubmitOp;
  timeout(timeoutMs: number): RequestCallbackSubmitOp;
  flags(flags: SendFlags): RequestCallbackSubmitOp;
  submit(callback: RequestCallback): boolean;
}

export interface ReplyOp {
  message(message: MessageLike): ReplySubmitOp;
}

export interface ReplySubmitOp {
  message(message: MessageLike): ReplySubmitOp;
  flags(flags: SendFlags): ReplySubmitOp;
  submit(): void;
}
