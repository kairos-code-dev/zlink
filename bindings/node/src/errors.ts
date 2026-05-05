// SPDX-License-Identifier: MPL-2.0

export const SubmitResult = Object.freeze({
  Ok: 0,
  Backpressured: 1,
  NotConnected: 2,
  NotFound: 3,
  Terminated: 4,
  InvalidHandle: 5,
  InvalidArgument: 6,
  NotSupported: 7,
  InvalidState: 8,
  ThreadViolation: 9,
  OutOfMemory: 10,
  SeqExhausted: 11,
  InternalError: 12,
  NotAdmitted: 13
} as const);
export type SubmitResult = typeof SubmitResult[keyof typeof SubmitResult];

export const RequestResult = Object.freeze({
  Ok: 0,
  TimedOut: 101,
  NotFound: 102,
  Terminated: 103,
  ProtocolError: 104,
  InternalError: 105,
  Rejected: 106,
  Conflict: 107,
  Busy: 108,
  NotConnected: 109,
  InvalidArgument: 110,
  InvalidState: 111,
  NotSupported: 112
} as const);
export type RequestResult = typeof RequestResult[keyof typeof RequestResult];

export const RecvResult = Object.freeze({
  Ok: 0,
  NoData: 201,
  Busy: 202,
  Terminated: 203,
  InvalidHandle: 204,
  NotSupported: 205
} as const);
export type RecvResult = typeof RecvResult[keyof typeof RecvResult];

export const HandlerResult = Object.freeze({
  Ok: 0,
  InvalidArgument: 301,
  Busy: 302,
  NotSupported: 303,
  Deadlock: 304,
  InvalidHandle: 305
} as const);
export type HandlerResult = typeof HandlerResult[keyof typeof HandlerResult];

export const CloseResult = Object.freeze({
  Ok: 0,
  Busy: 401,
  Shutdown: 402,
  InvalidHandle: 403
} as const);
export type CloseResult = typeof CloseResult[keyof typeof CloseResult];

export const BindResult = Object.freeze({
  Ok: 0,
  InvalidArgument: 501,
  AddrInUse: 502,
  NotSupported: 503,
  InvalidHandle: 504
} as const);
export type BindResult = typeof BindResult[keyof typeof BindResult];

export const ConnectResult = Object.freeze({
  Ok: 0,
  InvalidArgument: 601,
  NotSupported: 602,
  InvalidHandle: 603
} as const);
export type ConnectResult = typeof ConnectResult[keyof typeof ConnectResult];

export const ConfigResult = Object.freeze({
  Ok: 0,
  InvalidHandle: 701,
  InvalidArgument: 702,
  NotSupported: 703
} as const);
export type ConfigResult = typeof ConfigResult[keyof typeof ConfigResult];

export class ZlinkError extends Error {
  readonly code: number;
  readonly internalErrno: number;

  constructor(code: number, internalErrno = 0, message?: string) {
    super(message ?? `zlink error ${code}`);
    this.name = 'ZlinkError';
    this.code = code | 0;
    this.internalErrno = internalErrno | 0;
  }
}

export class SubmitError extends ZlinkError {
  readonly result: SubmitResult;

  constructor(result: SubmitResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'SubmitError';
    this.result = result;
  }
}

export class RequestError extends ZlinkError {
  readonly result: RequestResult;

  constructor(result: RequestResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'RequestError';
    this.result = result;
  }
}

export class RecvError extends ZlinkError {
  readonly result: RecvResult;

  constructor(result: RecvResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'RecvError';
    this.result = result;
  }
}

export class HandlerError extends ZlinkError {
  readonly result: HandlerResult;

  constructor(result: HandlerResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'HandlerError';
    this.result = result;
  }
}

export class CloseError extends ZlinkError {
  readonly result: CloseResult;

  constructor(result: CloseResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'CloseError';
    this.result = result;
  }
}

export class BindError extends ZlinkError {
  readonly result: BindResult;

  constructor(result: BindResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'BindError';
    this.result = result;
  }
}

export class ConnectError extends ZlinkError {
  readonly result: ConnectResult;

  constructor(result: ConnectResult, internalErrno = 0, message?: string) {
    super(result, internalErrno, message);
    this.name = 'ConnectError';
    this.result = result;
  }
}

export class ConfigError extends ZlinkError {
  readonly result: ConfigResult;

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
        default: return RecvResult.NotSupported;
      }
    case 'handler':
      switch (errno) {
        case 0: return HandlerResult.Ok;
        case 22: return HandlerResult.InvalidArgument;
        case 16: return HandlerResult.Busy;
        case 35: return HandlerResult.Deadlock;
        case 14: return HandlerResult.InvalidHandle;
        default: return HandlerResult.NotSupported;
      }
    case 'close':
      switch (errno) {
        case 0: return CloseResult.Ok;
        case 16: return CloseResult.Busy;
        case 108: return CloseResult.Shutdown;
        case 14: return CloseResult.InvalidHandle;
        default: return CloseResult.InvalidHandle;
      }
    case 'bind':
      switch (errno) {
        case 0: return BindResult.Ok;
        case 22: return BindResult.InvalidArgument;
        case 98: return BindResult.AddrInUse;
        case 95:
        case 93: return BindResult.NotSupported;
        case 14: return BindResult.InvalidHandle;
        default: return BindResult.InvalidArgument;
      }
    case 'connect':
      switch (errno) {
        case 0: return ConnectResult.Ok;
        case 22: return ConnectResult.InvalidArgument;
        case 95:
        case 93: return ConnectResult.NotSupported;
        case 14: return ConnectResult.InvalidHandle;
        default: return ConnectResult.InvalidArgument;
      }
    case 'config':
      switch (errno) {
        case 0: return ConfigResult.Ok;
        case 14: return ConfigResult.InvalidHandle;
        case 22: return ConfigResult.InvalidArgument;
        case 95:
        case 93: return ConfigResult.NotSupported;
        default: return ConfigResult.InvalidArgument;
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
