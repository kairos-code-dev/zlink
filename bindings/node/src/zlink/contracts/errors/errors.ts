// SPDX-License-Identifier: MPL-2.0

import {
  BindResult,
  CloseResult,
  ConfigResult,
  ConnectResult,
  HandlerResult,
  RecvResult,
  RequestResult,
  SubmitResult,
} from './results';
export {
  BindResult,
  CloseResult,
  ConfigResult,
  ConnectResult,
  HandlerResult,
  RecvResult,
  RequestResult,
  SubmitResult,
} from './results';

/** Base class for all errors thrown by the zlink bindings; carries a result `code` and the underlying native errno. */
export class ZlinkError extends Error {
  readonly code: number;
  readonly internalErrno: number;

  constructor(code: number, internalErrno?: number);
  /** @internal */
  constructor(code: number, internalErrno: number | undefined, message: string | undefined);
  constructor(code: number, internalErrno = 0, message?: string) {
    super(message ?? `zlink error ${code}`);
    this.name = 'ZlinkError';
    this.code = code | 0;
    this.internalErrno = internalErrno | 0;
  }
}

/** Thrown when submitting a send or publish fails. */
export class SubmitError extends ZlinkError {
  readonly result: SubmitResult;

  constructor(result: SubmitResult, internalErrno?: number);
  /** @internal */
  constructor(result: SubmitResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: SubmitResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'SubmitError';
    this.result = result;
  }
}

/** Thrown when a request fails or its reply reports an error. */
export class RequestError extends ZlinkError {
  readonly result: RequestResult;

  constructor(result: RequestResult, internalErrno?: number);
  /** @internal */
  constructor(result: RequestResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: RequestResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'RequestError';
    this.result = result;
  }
}

/** Thrown when receiving a message fails. */
export class RecvError extends ZlinkError {
  readonly result: RecvResult;

  constructor(result: RecvResult, internalErrno?: number);
  /** @internal */
  constructor(result: RecvResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: RecvResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'RecvError';
    this.result = result;
  }
}

/** Thrown when registering or running a callback handler fails. */
export class HandlerError extends ZlinkError {
  readonly result: HandlerResult;

  constructor(result: HandlerResult, internalErrno?: number);
  /** @internal */
  constructor(result: HandlerResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: HandlerResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'HandlerError';
    this.result = result;
  }
}

/** Thrown when closing a socket or resource fails. */
export class CloseError extends ZlinkError {
  readonly result: CloseResult;

  constructor(result: CloseResult, internalErrno?: number);
  /** @internal */
  constructor(result: CloseResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: CloseResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'CloseError';
    this.result = result;
  }
}

/** Thrown when binding a socket to an endpoint fails. */
export class BindError extends ZlinkError {
  readonly result: BindResult;

  constructor(result: BindResult, internalErrno?: number);
  /** @internal */
  constructor(result: BindResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: BindResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'BindError';
    this.result = result;
  }
}

/** Thrown when connecting a socket to an endpoint fails. */
export class ConnectError extends ZlinkError {
  readonly result: ConnectResult;

  constructor(result: ConnectResult, internalErrno?: number);
  /** @internal */
  constructor(result: ConnectResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: ConnectResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'ConnectError';
    this.result = result;
  }
}

/** Thrown when reading or applying a configuration option fails. */
export class ConfigError extends ZlinkError {
  readonly result: ConfigResult;

  constructor(result: ConfigResult, internalErrno?: number);
  /** @internal */
  constructor(result: ConfigResult, internalErrno: number | undefined, message: string | undefined);
  constructor(result: ConfigResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'ConfigError';
    this.result = result;
  }
}

type ErrorCategory =
  | 'submit'
  | 'request'
  | 'recv'
  | 'handler'
  | 'close'
  | 'bind'
  | 'connect'
  | 'config';

export function mapNativeErrno(category: ErrorCategory, errno: number): number {
  switch (category) {
    case 'submit':
      switch (errno) {
        case 0: return SubmitResult.Ok;
        case 11: return SubmitResult.Backpressured;
        case 107:
        case 113:
        case 110: return SubmitResult.NotConnected;
        case 111: return SubmitResult.NotAdmitted;
        case 2: return SubmitResult.NotFound;
        case 125: return SubmitResult.Terminated;
        case 14: return SubmitResult.InvalidHandle;
        case 22: return SubmitResult.InvalidArgument;
        case 95:
        case 93: return SubmitResult.NotSupported;
        case 16: return SubmitResult.InvalidState;
        case 35: return SubmitResult.ThreadViolation;
        case 12: return SubmitResult.OutOfMemory;
        case 75: return SubmitResult.SeqExhausted;
        default: return SubmitResult.InternalError;
      }
    case 'request':
      switch (errno) {
        case 0: return RequestResult.Ok;
        case 110: return RequestResult.TimedOut;
        case 2: return RequestResult.NotFound;
        case 125: return RequestResult.Terminated;
        case 111: return RequestResult.Rejected;
        case 17: return RequestResult.Conflict;
        case 16: return RequestResult.Busy;
        case 107:
        case 113: return RequestResult.NotConnected;
        case 22: return RequestResult.InvalidArgument;
        case 95:
        case 93: return RequestResult.NotSupported;
        default: return RequestResult.ProtocolError;
      }
    case 'recv':
      switch (errno) {
        case 0: return RecvResult.Ok;
        case 11: return RecvResult.NoData;
        case 4: return RecvResult.NoData;
        case 16: return RecvResult.Busy;
        case 125: return RecvResult.Terminated;
        case 14: return RecvResult.InvalidHandle;
        default: return RecvResult.InternalError;
      }
    case 'handler':
      switch (errno) {
        case 0: return HandlerResult.Ok;
        case 22: return HandlerResult.InvalidArgument;
        case 16: return HandlerResult.Busy;
        case 35: return HandlerResult.Deadlock;
        case 14: return HandlerResult.InvalidHandle;
        default: return HandlerResult.InternalError;
      }
    case 'close':
      switch (errno) {
        case 0: return CloseResult.Ok;
        case 16: return CloseResult.Busy;
        case 108: return CloseResult.Shutdown;
        case 14: return CloseResult.InvalidHandle;
        default: return CloseResult.InternalError;
      }
    case 'bind':
      switch (errno) {
        case 0: return BindResult.Ok;
        case 22: return BindResult.InvalidArgument;
        case 98: return BindResult.AddrInUse;
        case 95:
        case 93: return BindResult.NotSupported;
        case 14: return BindResult.InvalidHandle;
        default: return BindResult.InternalError;
      }
    case 'connect':
      switch (errno) {
        case 0: return ConnectResult.Ok;
        case 22: return ConnectResult.InvalidArgument;
        case 95:
        case 93: return ConnectResult.NotSupported;
        case 14: return ConnectResult.InvalidHandle;
        case 2: return ConnectResult.NotFound;
        case 17: return ConnectResult.Conflict;
        case 16: return ConnectResult.Busy;
        default: return ConnectResult.InternalError;
      }
    case 'config':
      switch (errno) {
        case 0: return ConfigResult.Ok;
        case 14: return ConfigResult.InvalidHandle;
        case 22: return ConfigResult.InvalidArgument;
        case 95:
        case 93: return ConfigResult.NotSupported;
        case 16: return ConfigResult.InvalidState;
        case 2: return ConfigResult.NotFound;
        default: return ConfigResult.InternalError;
      }
  }
}

export function createError(
  category: ErrorCategory,
  errno: number,
  message?: string
): ZlinkError {
  const code = mapNativeErrno(category, errno);
  switch (category) {
    case 'submit':
      return new SubmitError(code as SubmitResult, errno, message);
    case 'request':
      return new RequestError(code as RequestResult, errno, message);
    case 'recv':
      return new RecvError(code as RecvResult, errno, message);
    case 'handler':
      return new HandlerError(code as HandlerResult, errno, message);
    case 'close':
      return new CloseError(code as CloseResult, errno, message);
    case 'bind':
      return new BindError(code as BindResult, errno, message);
    case 'connect':
      return new ConnectError(code as ConnectResult, errno, message);
    case 'config':
      return new ConfigError(code as ConfigResult, errno, message);
  }
}
