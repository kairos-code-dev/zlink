import * as net from 'node:net';
import * as tls from 'node:tls';
import { ZlinkStreamConnection, ZlinkStreamErrorCode, ZlinkStreamException } from '../../Contracts';
import { connectorError, throwIfAborted } from '../ZlinkStreamSupport';
import { BufferedByteQueue } from './BufferedByteQueue';
import { concatParts, encodeWebSocketFrame, tryDecodeWebSocketFrame, WebSocketFrame } from './WebSocketFrameCodec';

export class NodeWebSocketConnection implements ZlinkStreamConnection {
  private readonly buffer = new BufferedByteQueue();
  private closed = false;
  private readWaiter: (() => void) | undefined;
  private error: Error | undefined;
  private readonly messageQueue: Uint8Array[] = [];
  private queuedMessageBytes = 0;
  private currentMessageParts: Uint8Array[] = [];

  constructor(
    private readonly socket: net.Socket | tls.TLSSocket,
    private readonly maxReceivePayloadSize: number,
    initialData?: Buffer
  ) {
    socket.on('data', (chunk: Buffer) => {
      this.buffer.push(chunk);
      this.drainFrames();
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

    if (initialData !== undefined && initialData.length > 0) {
      this.buffer.push(initialData);
      this.drainFrames();
    }
  }

  async write(frame: Uint8Array, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const encoded = encodeWebSocketFrame(frame, { opcode: 0x2, masked: true });
    await new Promise<void>((resolve, reject) => {
      this.socket.write(encoded, (error) => {
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
    for (;;) {
      const message = this.messageQueue.shift();
      if (message !== undefined) {
        this.queuedMessageBytes -= message.length;
        return message;
      }
      if (this.error !== undefined) {
        if (this.error instanceof ZlinkStreamException) {
          throw this.error;
        }
        throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Remote stream closed.', this.error);
      }
      if (this.closed) {
        return undefined;
      }
      await this.waitForData(signal);
    }
  }

  async close(): Promise<void> {
    if (!this.closed) {
      this.socket.write(encodeWebSocketFrame(new Uint8Array(), { opcode: 0x8, masked: true }));
    }
    this.closed = true;
    this.socket.destroy();
    this.wakeReader();
  }

  private drainFrames(): void {
    try {
      for (;;) {
        const frame = tryDecodeWebSocketFrame(this.buffer, this.maxReceivePayloadSize);
        if (frame === undefined) {
          return;
        }
        this.handleFrame(frame);
      }
    } catch (cause) {
      this.error = cause instanceof Error ? cause : connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'WebSocket frame decode failed.', cause);
      this.closed = true;
    }
  }

  private handleFrame(frame: WebSocketFrame): void {
    if (frame.opcode === 0x8) {
      this.closed = true;
      return;
    }
    if (frame.opcode === 0x9) {
      this.socket.write(encodeWebSocketFrame(frame.payload, { opcode: 0xa, masked: true }));
      return;
    }
    if (frame.opcode === 0xa) {
      return;
    }
    if (frame.opcode !== 0x0 && frame.opcode !== 0x2) {
      this.error = connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'WebSocket text messages are not supported.');
      this.closed = true;
      return;
    }

    if (frame.opcode === 0x2) {
      this.currentMessageParts = [];
    }
    const currentLength = this.currentMessageParts.reduce((sum, part) => sum + part.length, 0);
    if (currentLength + frame.payload.length > this.maxReceivePayloadSize) {
      throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket message exceeds MaxReceivePayloadSize.');
    }
    this.currentMessageParts.push(frame.payload);
    if (frame.fin) {
      const message = concatParts(this.currentMessageParts, this.maxReceivePayloadSize);
      if (this.queuedMessageBytes + message.length > this.maxReceivePayloadSize) {
        throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'WebSocket message queue exceeds MaxReceivePayloadSize.');
      }
      this.messageQueue.push(message);
      this.queuedMessageBytes += message.length;
      this.currentMessageParts = [];
    }
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
