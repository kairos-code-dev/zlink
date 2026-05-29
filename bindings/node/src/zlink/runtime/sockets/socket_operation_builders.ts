// SPDX-License-Identifier: MPL-2.0

import { Message, type MessageLike } from '../../contracts';
import { OperationPayload } from '../../contracts/messaging/operation_payload';
import { SendFlags } from '../../contracts/sockets/socket_constants';
import type {
  RequestCallback,
  RequestCallbackSubmitOperation,
  RequestOperation,
  RequestSubmitOperation,
  ReplyOperation,
  ReplySubmitOperation,
  SendOperation,
  SendSubmitOperation,
} from '../../contracts/service';

export type SendInvoker = (parts: readonly MessageLike[], flags: SendFlags) => boolean;
export type PublishInvoker = (
  topic: string,
  payload: MessageLike | readonly MessageLike[],
  flags: SendFlags
) => boolean;
export type RequestInvoker = (
  parts: readonly MessageLike[],
  callbackOrTimeout?: RequestCallback | number,
  flagsOrTimeout?: SendFlags | number,
  maybeTimeout?: number
) => Promise<Message[]> | boolean;
export type ReplyInvoker = (parts: readonly MessageLike[], flags: SendFlags) => void;

export class RuntimeSendOperation implements SendOperation, SendSubmitOperation {
  private readonly _invoke: SendInvoker;
  private readonly _payload = new OperationPayload<MessageLike, MessageLike>((message) => message);
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: SendInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): SendSubmitOperation {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): SendSubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    return this._invoke(this._payload.consume(), this._flags);
  }
}

export class PublishOperation implements SendOperation, SendSubmitOperation {
  private readonly _invoke: PublishInvoker;
  private readonly _topic: string;
  private _single: MessageLike | null = null;
  private _parts: MessageLike[] | null = null;
  private _submitted = false;
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: PublishInvoker, topic: string) {
    this._invoke = invoke;
    this._topic = topic;
  }

  message(message: MessageLike): SendSubmitOperation {
    this.ensureOpen();
    if (this._parts) {
      this._parts.push(message);
    } else if (this._single) {
      this._parts = [this._single, message];
      this._single = null;
    } else {
      this._single = message;
    }
    return this;
  }

  flags(flags: SendFlags): SendSubmitOperation {
    this.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    this.ensureOpen();
    const payload = this._parts ?? this._single;
    if (!payload) {
      throw new TypeError('operation requires at least one message');
    }
    this._submitted = true;
    return this._invoke(this._topic, payload, this._flags);
  }

  private ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }
}

export class RuntimeRequestOperation implements RequestOperation, RequestSubmitOperation, RequestCallbackSubmitOperation {
  private readonly _invoke: RequestInvoker;
  private readonly _payload = new OperationPayload<MessageLike, MessageLike>((message) => message);
  private _timeoutMs = 0;
  private _flags: SendFlags = SendFlags.None;
  private _callbackMode = false;

  constructor(invoke: RequestInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): RequestSubmitOperation {
    this._payload.append(message);
    return this;
  }

  timeout(timeoutMs: number): RequestSubmitOperation {
    this._payload.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  flags(flags: SendFlags): RequestCallbackSubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    this._callbackMode = true;
    return this;
  }

  submitAsync(): Promise<Message[]> {
    return this._invoke(this._payload.consume(), this._timeoutMs) as Promise<Message[]>;
  }

  submit(callback: RequestCallback): boolean {
    const flags = this._callbackMode ? this._flags : SendFlags.None;
    return this._invoke(this._payload.consume(), callback, flags, this._timeoutMs) as boolean;
  }
}

export class RuntimeReplyOperation implements ReplyOperation, ReplySubmitOperation {
  private readonly _invoke: ReplyInvoker;
  private readonly _payload = new OperationPayload<MessageLike, MessageLike>((message) => message);
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: ReplyInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): ReplySubmitOperation {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReplySubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume(), this._flags);
  }
}
