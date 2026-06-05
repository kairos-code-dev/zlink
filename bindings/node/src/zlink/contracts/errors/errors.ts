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
  readonly nativeErrno: number;

  constructor(code: number, nativeErrno = 0) {
    super(`zlink error ${code}`);
    this.name = 'ZlinkError';
    this.code = code | 0;
    this.nativeErrno = nativeErrno | 0;
  }
}

/** Thrown when submitting a send or publish fails. */
export class SubmitError extends ZlinkError {
  readonly result: SubmitResult;

  constructor(result: SubmitResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'SubmitError';
    this.result = result;
  }
}

/** Thrown when a request fails or its reply reports an error. */
export class RequestError extends ZlinkError {
  readonly result: RequestResult;

  constructor(result: RequestResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'RequestError';
    this.result = result;
  }
}

/** Thrown when receiving a message fails. */
export class RecvError extends ZlinkError {
  readonly result: RecvResult;

  constructor(result: RecvResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'RecvError';
    this.result = result;
  }
}

/** Thrown when registering or running a callback handler fails. */
export class HandlerError extends ZlinkError {
  readonly result: HandlerResult;

  constructor(result: HandlerResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'HandlerError';
    this.result = result;
  }
}

/** Thrown when closing a socket or resource fails. */
export class CloseError extends ZlinkError {
  readonly result: CloseResult;

  constructor(result: CloseResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'CloseError';
    this.result = result;
  }
}

/** Thrown when binding a socket to an endpoint fails. */
export class BindError extends ZlinkError {
  readonly result: BindResult;

  constructor(result: BindResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'BindError';
    this.result = result;
  }
}

/** Thrown when connecting a socket to an endpoint fails. */
export class ConnectError extends ZlinkError {
  readonly result: ConnectResult;

  constructor(result: ConnectResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'ConnectError';
    this.result = result;
  }
}

/** Thrown when reading or applying a configuration option fails. */
export class ConfigError extends ZlinkError {
  readonly result: ConfigResult;

  constructor(result: ConfigResult, nativeErrno = 0) {
    super(result, nativeErrno);
    this.name = 'ConfigError';
    this.result = result;
  }
}
