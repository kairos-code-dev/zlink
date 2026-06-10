// SPDX-License-Identifier: MPL-2.0

import { Message, type MessageLike } from '../../../contracts';
import { RequestResult } from '../../../contracts/errors/errors';
import { SendFlags } from '../../../contracts/sockets/socket_constants';
import type {
  ActorBindOperation,
  ActorDestroyOperation,
  ActorJoinCallbackSubmitOperation,
  ActorJoinEntrySpotHandler,
  ActorJoinEntrySpotOperation,
  ActorJoinEntrySpotResult,
  ActorJoinHandler,
  ActorJoinOperation,
  ActorJoinReplyOperation,
  ActorJoinResult,
  ActorJoinSubmitOperation,
  ActorLeaveOperation,
  ActorLookupHandler,
  ActorLookupOperation,
  ActorLookupResult,
  ActorUnbindOperation,
  ReplyHandler
} from '../../../contracts/service';
import { requestErrorFromResult } from '../../messaging/request_executor';
import { OperationPayload, type OperationPayloadValue } from '../../messaging/operation_payload';

type ActorJoinInvoker = (
  parts: OperationPayloadValue<MessageLike>,
  callback: ActorJoinHandler,
  flags: SendFlags,
  timeoutMs: number,
) => boolean;

export class RuntimeActorJoinOperation implements ActorJoinOperation, ActorJoinSubmitOperation, ActorJoinCallbackSubmitOperation {
  private readonly _invoke: ActorJoinInvoker;
  private readonly _payload = new OperationPayload<MessageLike, MessageLike>((message) => message);
  private _flags: SendFlags = SendFlags.None;
  private _timeoutMs = 0;
  private _callbackMode = false;

  constructor(invoke: ActorJoinInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): this {
    this._payload.append(message);
    return this;
  }

  timeout(timeoutMs: number): this {
    this._payload.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  flags(flags: SendFlags): ActorJoinCallbackSubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    this._callbackMode = true;
    return this;
  }

  submit(): Promise<{ result: ActorJoinResult; parts: Message[] }>;
  submit(callback: ActorJoinHandler): boolean;
  submit(callback?: ActorJoinHandler): Promise<{ result: ActorJoinResult; parts: Message[] }> | boolean {
    if (callback !== undefined) {
      const flags = this._callbackMode ? this._flags : SendFlags.None;
      return this._invoke(this._payload.consume(), callback, flags, this._timeoutMs);
    }
    const parts = this._payload.consume();
    return new Promise((resolve, reject) => {
      this._invoke(parts, (result, replyParts) => {
        if (result.result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result.result, 'actor join failed'));
          return;
        }
        resolve({ result, parts: replyParts });
      }, SendFlags.None, this._timeoutMs);
    });
  }
}

type ActorJoinEntrySpotInvoker = (
  callback: ActorJoinEntrySpotHandler,
  timeoutMs: number,
) => boolean;

export class RuntimeActorJoinReplyOperation implements ActorJoinReplyOperation {
  private readonly _invoke: (parts: OperationPayloadValue<MessageLike>) => void;
  private readonly _payload = new OperationPayload<MessageLike, MessageLike>((message) => message);

  constructor(invoke: (parts: OperationPayloadValue<MessageLike>) => void) {
    this._invoke = invoke;
  }

  message(message: MessageLike): ActorJoinReplyOperation {
    this._payload.append(message);
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume());
  }
}

type ReplyOpInvoker = (callback: ReplyHandler, timeoutMs: number) => boolean;

export class ReplyHandlerOperation {
  protected readonly _invoke: ReplyOpInvoker;
  protected _timeoutMs = 0;
  protected _submitted = false;

  constructor(invoke: ReplyOpInvoker) {
    this._invoke = invoke;
  }

  protected ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  protected markSubmitted(): void {
    this._submitted = true;
  }

  timeout(timeoutMs: number): this {
    this.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  submit(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
  submit(callback?: ReplyHandler): Promise<Message[]> | boolean {
    this.ensureOpen();
    if (callback !== undefined) {
      this.markSubmitted();
      return this._invoke(callback, this._timeoutMs);
    }
    return new Promise((resolve, reject) => {
      this.markSubmitted();
      this._invoke((result, parts) => {
        if (result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result, 'actor operation failed'));
          return;
        }
        resolve(parts);
      }, this._timeoutMs);
    });
  }
}

export class RuntimeActorLeaveOperation extends ReplyHandlerOperation implements ActorLeaveOperation {}
export class RuntimeActorDestroyOperation extends ReplyHandlerOperation implements ActorDestroyOperation {}
export class RuntimeActorBindOperation extends ReplyHandlerOperation implements ActorBindOperation {}
export class RuntimeActorUnbindOperation extends ReplyHandlerOperation implements ActorUnbindOperation {}

type ResultHandlerInvoker<TResult extends { result: RequestResult }> = (
  callback: (result: TResult) => void,
  timeoutMs: number,
) => boolean;

class ResultHandlerOperation<TResult extends { result: RequestResult }> {
  private readonly _invoke: ResultHandlerInvoker<TResult>;
  private readonly _errorMessage: string;
  private _timeoutMs = 0;
  private _submitted = false;

  constructor(invoke: ResultHandlerInvoker<TResult>, errorMessage: string) {
    this._invoke = invoke;
    this._errorMessage = errorMessage;
  }

  protected ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  timeout(timeoutMs: number): this {
    this.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  submit(): Promise<TResult>;
  submit(callback: (result: TResult) => void): boolean;
  submit(callback?: (result: TResult) => void): Promise<TResult> | boolean {
    this.ensureOpen();
    this._submitted = true;
    if (callback !== undefined) {
      return this._invoke(callback, this._timeoutMs);
    }
    return new Promise((resolve, reject) => {
      this._invoke((result) => {
        if (result.result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result.result, this._errorMessage));
          return;
        }
        resolve(result);
      }, this._timeoutMs);
    });
  }
}

export class RuntimeActorJoinEntrySpotOperation
  extends ResultHandlerOperation<ActorJoinEntrySpotResult>
  implements ActorJoinEntrySpotOperation {
  constructor(invoke: ActorJoinEntrySpotInvoker) {
    super(invoke, 'actor entry spot join failed');
  }
}

type ActorLookupInvoker = (callback: ActorLookupHandler, timeoutMs: number) => boolean;

export class RuntimeActorLookupOperation
  extends ResultHandlerOperation<ActorLookupResult>
  implements ActorLookupOperation {
  constructor(invoke: ActorLookupInvoker) {
    super(invoke, 'actor lookup failed');
  }
}
