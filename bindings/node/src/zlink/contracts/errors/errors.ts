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
