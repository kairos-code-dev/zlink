import { ZlinkStreamEncodedPayload, ZlinkStreamError, ZlinkStreamErrorCode } from '../Contracts';
import { connectorError } from './ZlinkStreamSupport';

export interface PendingZlinkStreamRequest {
  readonly requestSeq: bigint;
  readonly promise: Promise<ZlinkStreamEncodedPayload>;
}

interface TrackedPendingRequest {
  readonly packetName: string;
  readonly promise: Promise<ZlinkStreamEncodedPayload>;
  resolve(value: ZlinkStreamEncodedPayload): void;
  reject(error: ZlinkStreamError): void;
  cancel(): void;
}

export class ZlinkStreamPendingRequests {
  private nextRequestSeq = 1n;
  private readonly active = new Map<bigint, TrackedPendingRequest>();

  get count(): number {
    return this.active.size;
  }

  create(packetName: string, timeoutMs: number): PendingZlinkStreamRequest {
    const requestSeq = this.nextRequestSeq++;
    let timeout: ReturnType<typeof setTimeout> | undefined;
    let resolvePending!: (value: ZlinkStreamEncodedPayload) => void;
    let rejectPending!: (error: ZlinkStreamError) => void;
    const promise = new Promise<ZlinkStreamEncodedPayload>((resolve, reject) => {
      timeout = setTimeout(() => {
        this.active.delete(requestSeq);
        reject(connectorError(ZlinkStreamErrorCode.RequestTimeout, 'Request timed out.'));
      }, timeoutMs);
      resolvePending = resolve;
      rejectPending = (error) => reject(connectorError(error.code, error.message, error.cause));
    });
    this.active.set(requestSeq, {
      packetName,
      promise,
      resolve: (value) => {
        if (timeout !== undefined) {
          clearTimeout(timeout);
        }
        resolvePending(value);
      },
      reject: (error) => {
        if (timeout !== undefined) {
          clearTimeout(timeout);
        }
        rejectPending(error);
      },
      cancel: () => {
        if (timeout !== undefined) {
          clearTimeout(timeout);
        }
      }
    });
    return { requestSeq, promise };
  }

  resolve(requestSeq: bigint, packetName: string, value: ZlinkStreamEncodedPayload): boolean {
    const pending = this.active.get(requestSeq);
    if (pending === undefined) {
      return false;
    }
    this.active.delete(requestSeq);
    if (pending.packetName !== packetName) {
      pending.reject(packetNameMismatch(pending.packetName, packetName));
      return true;
    }
    pending.resolve(value);
    return true;
  }

  reject(requestSeq: bigint, packetName: string, error: ZlinkStreamError): boolean {
    const pending = this.active.get(requestSeq);
    if (pending === undefined) {
      return false;
    }
    this.active.delete(requestSeq);
    if (pending.packetName !== packetName) {
      pending.reject(packetNameMismatch(pending.packetName, packetName));
      return true;
    }
    pending.reject(error);
    return true;
  }

  cancel(requestSeq: bigint): void {
    const pending = this.active.get(requestSeq);
    if (pending === undefined) {
      return;
    }
    this.active.delete(requestSeq);
    pending.cancel();
  }

  failAll(error: ZlinkStreamError): void {
    for (const [requestSeq, pending] of this.active) {
      this.active.delete(requestSeq);
      pending.reject(error);
    }
  }
}

function packetNameMismatch(expected: string, actual: string): ZlinkStreamError {
  return {
    code: ZlinkStreamErrorCode.FrameDecodeFailed,
    message: `Reply packet name '${actual}' does not match request packet name '${expected}'.`
  };
}
