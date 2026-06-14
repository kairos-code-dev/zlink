// SPDX-License-Identifier: MPL-2.0

import type { BufferLike } from '../../contracts/core/buffer_like';
import { Message } from '../../contracts/messaging/message';
import type {
  ReplyOperation,
  ReplySubmitOperation,
  SendOperation,
  SendSubmitOperation,
} from '../../contracts/messaging/operations';
import { SendFlags } from '../../contracts/sockets/socket_constants';
import { OperationPayload } from './operation_payload';

class RuntimeReceivedSendOperation implements SendOperation, SendSubmitOperation {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => boolean;
  private readonly _payload = new OperationPayload<Message | BufferLike, Message>(
    (message) => message instanceof Message ? message : Message.from(message)
  );
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: (parts: readonly Message[], flags: SendFlags) => boolean) {
    this._invoke = invoke;
  }

  message(message: Message | BufferLike): SendSubmitOperation {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): SendSubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    return this._invoke(this._payload.consumeParts(), this._flags);
  }
}

class RuntimeReceivedReplyOperation implements ReplyOperation, ReplySubmitOperation {
  private readonly _invoke: (parts: readonly Message[], flags: SendFlags) => void;
  private readonly _payload = new OperationPayload<Message | BufferLike, Message>(
    (message) => message instanceof Message ? message : Message.from(message)
  );
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: (parts: readonly Message[], flags: SendFlags) => void) {
    this._invoke = invoke;
  }

  message(message: Message | BufferLike): ReplySubmitOperation {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReplySubmitOperation {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consumeParts(), this._flags);
  }
}

export function createReceivedSendOperation(
  invoke: (parts: readonly Message[], flags: SendFlags) => boolean
): SendOperation {
  return new RuntimeReceivedSendOperation(invoke);
}

export function createReceivedReplyOperation(
  invoke: (parts: readonly Message[], flags: SendFlags) => void
): ReplyOperation {
  return new RuntimeReceivedReplyOperation(invoke);
}
