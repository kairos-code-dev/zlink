// SPDX-License-Identifier: MPL-2.0

import { Message } from '../../contracts';
import { RequestError, RequestResult, SubmitResult } from '../../contracts/errors/errors';
import { SendFlags } from '../../contracts/sockets/socket_constants';
import { submitNativeError } from '../errors/native_errors';

export type RequestCallback = (result: RequestResult, replyParts: Message[]) => void;

export interface NativeRequestOptions {
  callbackOrTimeout?: RequestCallback | number;
  flagsOrTimeout?: SendFlags | number;
  maybeTimeout?: number;
  startProgress: () => () => void;
  invoke: (
    callback: (result: number, replyParts: Buffer[] | null) => void,
    flags: SendFlags,
    timeoutMs: number
  ) => void;
  submitErrorMessage: string;
  requestErrorMessage: string;
  promiseTimeoutMayUseFlagsOrTimeout?: boolean;
}

export function normalizeCallbackFlagsAndTimeout(
  flagsOrTimeout?: SendFlags | number,
  maybeTimeout?: number,
): { flags: SendFlags; timeoutMs: number } {
  if (typeof maybeTimeout === 'number') {
    return {
      flags: (flagsOrTimeout ?? SendFlags.None) as SendFlags,
      timeoutMs: maybeTimeout | 0,
    };
  }
  if (typeof flagsOrTimeout === 'number') {
    if (flagsOrTimeout === SendFlags.None || flagsOrTimeout === SendFlags.DontWait) {
      return { flags: flagsOrTimeout as SendFlags, timeoutMs: 0 };
    }
    return { flags: SendFlags.None, timeoutMs: flagsOrTimeout | 0 };
  }
  return { flags: SendFlags.None, timeoutMs: 0 };
}

export function messagesFromNativeBuffers(buffers: readonly Buffer[] | null | undefined): Message[] {
  return (buffers ?? []).map((buffer) => Message.fromSnapshot({ data: buffer ?? Buffer.alloc(0) }));
}

export function requestErrorFromResult(result: RequestResult, message: string): RequestError {
  return new RequestError(result, 0, message);
}

export function executeNativeRequest(options: NativeRequestOptions): Promise<Message[]> | boolean {
  if (typeof options.callbackOrTimeout === 'function') {
    const callback = options.callbackOrTimeout;
    const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(
      options.flagsOrTimeout,
      options.maybeTimeout
    );
    const releaseProgress = options.startProgress();
    try {
      options.invoke(
        (result, replyParts) => {
          releaseProgress();
          callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
        },
        flags,
        timeoutMs
      );
      return true;
    } catch (error) {
      releaseProgress();
      const submitError = submitNativeError(error, flags, options.submitErrorMessage);
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }

  const timeoutMs = typeof options.callbackOrTimeout === 'number'
    ? options.callbackOrTimeout
    : options.promiseTimeoutMayUseFlagsOrTimeout
      ? options.flagsOrTimeout ?? 0
      : 0;

  return new Promise<Message[]>((resolve, reject) => {
    const releaseProgress = options.startProgress();
    try {
      options.invoke(
        (result, replyParts) => {
          releaseProgress();
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, options.requestErrorMessage));
            return;
          }
          resolve(messagesFromNativeBuffers(replyParts));
        },
        SendFlags.None,
        timeoutMs | 0
      );
    } catch (error) {
      releaseProgress();
      reject(submitNativeError(error, SendFlags.None, options.submitErrorMessage));
    }
  });
}
