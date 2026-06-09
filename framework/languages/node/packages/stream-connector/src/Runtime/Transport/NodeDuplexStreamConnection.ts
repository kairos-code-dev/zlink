import * as net from 'node:net';
import * as tls from 'node:tls';
import { ZlinkStreamConnection, ZlinkStreamErrorCode } from '../../Contracts';
import { connectorError, readUInt16BE, readUInt32BE, throwIfAborted } from '../ZlinkStreamSupport';
import { BufferedByteQueue } from './BufferedByteQueue';

export class NodeDuplexStreamConnection implements ZlinkStreamConnection {
  private readonly buffer = new BufferedByteQueue();
  private closed = false;
  private readWaiter: (() => void) | undefined;
  private error: Error | undefined;

  constructor(private readonly socket: net.Socket | tls.TLSSocket) {
    socket.on('data', (chunk: Buffer) => {
      this.buffer.push(chunk);
      this.wakeReader();
    });
    socket.on('close', () => {
      this.closed = true;
      this.wakeReader();
    });
    socket.on('error', (error) => {
      this.error = error;
      this.closed = true;
      this.wakeReader();
    });
  }

  async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    await new Promise<void>((resolve, reject) => {
      this.socket.write(frame, (error) => {
        if (error != null) {
          reject(connectorError(ZlinkStreamErrorCode.SendFailed, 'Send failed.', error));
          return;
        }
        resolve();
      });
    });
  }

  async read(signal?: AbortSignal): Promise<Uint8Array | undefined> {
    throwIfAborted(signal);
    while (true) {
      const frame = this.tryReadFrame();
      if (frame !== undefined) {
        return frame;
      }
      if (this.error !== undefined) {
        throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Remote stream closed.', this.error);
      }
      if (this.closed) {
        return undefined;
      }
      await this.waitForData(signal);
    }
  }

  async close(): Promise<void> {
    this.closed = true;
    this.socket.destroy();
    this.wakeReader();
  }

  private tryReadFrame(): Uint8Array | undefined {
    if (this.buffer.size < 6) {
      return undefined;
    }
    const prefix = this.buffer.peek(6);
    const headerLength = readUInt16BE(prefix, 0);
    const payloadLength = readUInt32BE(prefix, 2);
    const frameLength = 6 + headerLength + payloadLength;
    if (this.buffer.size < frameLength) {
      return undefined;
    }
    return this.buffer.consume(frameLength);
  }

  private waitForData(signal: AbortSignal | undefined): Promise<void> {
    if (this.readWaiter !== undefined) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Only one pending stream read is supported.');
    }
    return new Promise((resolve, reject) => {
      const onAbort = () => {
        this.readWaiter = undefined;
        reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.'));
      };
      if (signal !== undefined) {
        signal.addEventListener('abort', onAbort, { once: true });
      }
      this.readWaiter = () => {
        if (signal !== undefined) {
          signal.removeEventListener('abort', onAbort);
        }
        this.readWaiter = undefined;
        resolve();
      };
    });
  }

  private wakeReader(): void {
    this.readWaiter?.();
  }
}
