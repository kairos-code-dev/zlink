import {
  Disposable,
  RequiredZlinkStreamConnectorOptions,
  ZlinkStreamCodec,
  ZlinkStreamConnection,
  ZlinkStreamConnectionState,
  ZlinkStreamConnectionStateChanged,
  ZlinkStreamConnector,
  ZlinkStreamConnectorOptions,
  ZlinkStreamDispatchMode,
  ZlinkStreamEncodedPayload,
  ZlinkStreamError,
  ZlinkStreamErrorCode,
  ZlinkStreamException,
  ZlinkStreamHeader,
  ZlinkStreamHeaderFlags,
  ZlinkStreamMessage,
  ZlinkStreamMessageKind,
  ZlinkStreamMetadata,
  ZlinkStreamMetadataMap,
  ZlinkStreamRequestCall,
  ZlinkStreamSendCall
} from './contracts';
import { ZlinkStreamRequestBuilder, ZlinkStreamSendBuilder } from './calls';
import { buildHeader, validateName, ZlinkStreamFrameCodec, ZlinkStreamHeaderCodec } from './protocol';
import { normalizeOptions } from './options';
import { connectorError, delay, subscription, throwIfAborted, toStreamError, utf8Decode } from './support';
import { compressPayload, decompressIfNeeded } from './compression';

export const zlinkStreamConnectorFactory = {
  create(options: ZlinkStreamConnectorOptions): ZlinkStreamConnector {
    return new DefaultZlinkStreamConnector(options);
  }
};

export class DefaultZlinkStreamConnector implements ZlinkStreamConnector {
  static readonly heartbeatPingName = '$zlink.heartbeat.ping';
  static readonly heartbeatPongName = '$zlink.heartbeat.pong';

  private readonly handlers = new Map<string, Set<(message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal) => Promise<void> | void>>();
  private readonly errorHandlers = new Set<(error: ZlinkStreamError, signal?: AbortSignal) => Promise<void> | void>();
  private readonly disconnectedHandlers = new Set<(signal?: AbortSignal) => Promise<void> | void>();
  private readonly stateHandlers = new Set<(change: ZlinkStreamConnectionStateChanged, signal?: AbortSignal) => Promise<void> | void>();
  private connection: ZlinkStreamConnection | undefined;
  private currentState = ZlinkStreamConnectionState.Created;
  private nextRequestSeq = 1n;
  private readonly pendingRequests = new Map<bigint, PendingRequest>();
  private heartbeatTimer: NodeJS.Timeout | undefined;
  private receiveLoopAbort: AbortController | undefined;
  private lastInboundAt = 0;

  readonly options: RequiredZlinkStreamConnectorOptions;

  constructor(options: ZlinkStreamConnectorOptions) {
    this.options = normalizeOptions(options);
  }

  get isConnected(): boolean {
    return this.currentState === ZlinkStreamConnectionState.Connected;
  }

  get state(): ZlinkStreamConnectionState {
    return this.currentState;
  }

  get pendingDispatchCount(): number {
    return this.pendingRequests.size;
  }

  onErrorReceived(handler: (error: ZlinkStreamError, signal?: AbortSignal) => Promise<void> | void): Disposable {
    this.errorHandlers.add(handler);
    return subscription(() => this.errorHandlers.delete(handler));
  }

  onDisconnected(handler: (signal?: AbortSignal) => Promise<void> | void): Disposable {
    this.disconnectedHandlers.add(handler);
    return subscription(() => this.disconnectedHandlers.delete(handler));
  }

  onConnectionStateChanged(handler: (change: ZlinkStreamConnectionStateChanged, signal?: AbortSignal) => Promise<void> | void): Disposable {
    this.stateHandlers.add(handler);
    return subscription(() => this.stateHandlers.delete(handler));
  }

  async connect(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (this.currentState === ZlinkStreamConnectionState.Closed) {
      throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Connector is closed.');
    }
    if (this.currentState === ZlinkStreamConnectionState.Connected) {
      return;
    }

    await this.setState(ZlinkStreamConnectionState.Connecting, undefined, signal);
    try {
      this.connection = await this.connectWithReconnect(signal);
      this.lastInboundAt = Date.now();
      await this.setState(ZlinkStreamConnectionState.Connected, undefined, signal);
      this.startHeartbeat();
      this.startReceiveLoop();
    } catch (cause) {
      const error = toStreamError(cause, ZlinkStreamErrorCode.ConnectTimeout, 'Connect failed.');
      await this.setState(ZlinkStreamConnectionState.Disconnected, error, signal);
      throw new ZlinkStreamException(error);
    }
  }

  async close(signal?: AbortSignal): Promise<void> {
    if (this.currentState === ZlinkStreamConnectionState.Closed) {
      return;
    }
    const connection = this.connection;
    this.connection = undefined;
    this.stopHeartbeat();
    this.stopReceiveLoop();
    if (connection !== undefined) {
      await connection.close(signal);
    }
    this.failPending({ code: ZlinkStreamErrorCode.Disconnected, message: 'Connector closed.' });
    await this.setState(ZlinkStreamConnectionState.Closed, undefined, signal);
    await Promise.all([...this.disconnectedHandlers].map((handler) => handler(signal)));
  }

  async dispatch(signal?: AbortSignal): Promise<void> {
    await this.dispatchAvailable(signal);
  }

  private async dispatchAvailable(signal?: AbortSignal): Promise<boolean> {
    throwIfAborted(signal);
    const connection = this.connection;
    if (connection?.read === undefined) {
      return false;
    }
    const frameBytes = await connection.read(signal);
    if (frameBytes === undefined) {
      return false;
    }
    try {
      const frame = ZlinkStreamFrameCodec.decode(frameBytes);
      const header = ZlinkStreamHeaderCodec.decode(frame.header);
      this.lastInboundAt = Date.now();
      await this.dispatchFrame(header, frame.payload, signal);
    } catch (cause) {
      await this.publishError(toStreamError(cause, ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame decode failed.'), signal);
    }
    return true;
  }

  send(payload: ZlinkStreamEncodedPayload): ZlinkStreamSendCall {
    return new ZlinkStreamSendBuilder(this, this.resolveNameOrDefault(payload), payload);
  }

  request(payload: ZlinkStreamEncodedPayload): ZlinkStreamRequestCall {
    return new ZlinkStreamRequestBuilder(this, this.resolveNameOrDefault(payload), payload);
  }

  on(name: string, handler: (message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal) => Promise<void> | void): Disposable {
    validateName(name);
    let set = this.handlers.get(name);
    if (set === undefined) {
      set = new Set();
      this.handlers.set(name, set);
    }
    set.add(handler);
    return subscription(() => set?.delete(handler));
  }

  async sendEncoded(
    kind: ZlinkStreamMessageKind,
    name: string,
    payload: ZlinkStreamEncodedPayload,
    metadata: ZlinkStreamMetadata,
    compress: boolean,
    requestSeq: bigint | undefined,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const payloadBytes = compress ? compressPayload(payload.payload, this.options.compression) : payload.payload;
    const header = buildHeader(kind, name, payload.codec, metadata, compress, requestSeq);
    await this.sendFrame(header, payloadBytes, signal);
  }

  async requestEncoded(
    name: string,
    payload: ZlinkStreamEncodedPayload,
    metadata: ZlinkStreamMetadata,
    compress: boolean,
    timeoutMs: number,
    signal?: AbortSignal
  ): Promise<ZlinkStreamEncodedPayload> {
    const requestSeq = this.nextRequestSeq++;
    const pending = this.trackPending(requestSeq, timeoutMs);
    try {
      await this.sendEncoded(ZlinkStreamMessageKind.Request, name, payload, metadata, compress, requestSeq, signal);
      return await pending.promise;
    } catch (error) {
      this.pendingRequests.delete(requestSeq);
      pending.cancel();
      throw error;
    }
  }

  private async sendFrame(header: ZlinkStreamHeader, payload: Uint8Array, signal?: AbortSignal): Promise<void> {
    const connection = this.connection;
    if (connection === undefined || this.currentState !== ZlinkStreamConnectionState.Connected) {
      throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Connector is not connected.');
    }
    const headerBytes = ZlinkStreamHeaderCodec.encode(header);
    const frame = ZlinkStreamFrameCodec.encode(headerBytes, payload, this.options.maxSendPayloadSize);
    await connection.write(frame, signal);
  }

  private resolveNameOrDefault(payload: ZlinkStreamEncodedPayload): string | undefined {
    if (payload.messageType === undefined) {
      return undefined;
    }
    return this.options.nameResolver.resolve(payload.messageType);
  }

  private async dispatchFrame(header: ZlinkStreamHeader, payload: Uint8Array, signal?: AbortSignal): Promise<void> {
    if (header.kind === ZlinkStreamMessageKind.Response && header.requestSeq !== undefined) {
      const pending = this.pendingRequests.get(header.requestSeq);
      if (pending !== undefined) {
        this.pendingRequests.delete(header.requestSeq);
        try {
          pending.resolve({ codec: header.codec, payload: this.payloadForHeader(header, payload) });
        } catch (cause) {
          pending.reject(toStreamError(cause, ZlinkStreamErrorCode.DecompressionFailed, 'Decompression failed.'));
        }
      }
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Error && header.requestSeq !== undefined) {
      const pending = this.pendingRequests.get(header.requestSeq);
      if (pending !== undefined) {
        this.pendingRequests.delete(header.requestSeq);
        pending.reject({ code: ZlinkStreamErrorCode.RemoteError, message: utf8Decode(payload) });
      }
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Error) {
      await this.publishError({ code: ZlinkStreamErrorCode.RemoteError, message: utf8Decode(payload) }, signal);
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Control) {
      if (payload.length !== 0) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Control packet payload must be empty.');
      }
      if (header.name === DefaultZlinkStreamConnector.heartbeatPingName) {
        await this.sendControl(DefaultZlinkStreamConnector.heartbeatPongName, signal);
        return;
      }
      if (header.name === DefaultZlinkStreamConnector.heartbeatPongName) {
        return;
      }
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown control packet.');
    }
    if (header.kind === ZlinkStreamMessageKind.Send) {
      const handlers = this.handlers.get(header.name);
      if (handlers !== undefined) {
        const message = { name: header.name, metadata: header.metadata, payload: { codec: header.codec, payload: this.payloadForHeader(header, payload) } };
        for (const handler of handlers) {
          try {
            await handler(message, signal);
          } catch (cause) {
            await this.publishError({
              code: ZlinkStreamErrorCode.UserCallbackFailed,
              message: 'Typed message handler failed.',
              cause
            }, signal);
          }
        }
      }
    }
  }

  private async sendControl(name: string, signal?: AbortSignal): Promise<void> {
    await this.sendFrame({
      kind: ZlinkStreamMessageKind.Control,
      codec: ZlinkStreamCodec.Raw,
      flags: ZlinkStreamHeaderFlags.None,
      name,
      metadata: ZlinkStreamMetadataMap.empty
    }, new Uint8Array(), signal);
  }

  private payloadForHeader(header: ZlinkStreamHeader, payload: Uint8Array): Uint8Array {
    return decompressIfNeeded(header, payload, this.options.compression);
  }

  private async connectWithReconnect(signal?: AbortSignal): Promise<ZlinkStreamConnection> {
    let attempt = 0;
    let delayMs = this.options.reconnect.initialDelayMs;
    let lastError: ZlinkStreamError | undefined;
    const maxAttempts = this.options.reconnect.enabled ? this.options.reconnect.maxAttempts : 1;

    while (attempt < maxAttempts) {
      attempt += 1;
      try {
        return await this.options.transportFactory.connect(this.options, signal);
      } catch (cause) {
        lastError = toStreamError(cause, ZlinkStreamErrorCode.ConnectTimeout, 'Connect failed.');
        if (!this.options.reconnect.enabled || attempt >= maxAttempts) {
          break;
        }
        await this.setState(ZlinkStreamConnectionState.Reconnecting, lastError, signal);
        await delay(delayMs, signal);
        delayMs = Math.min(
          this.options.reconnect.maxDelayMs,
          Math.ceil(delayMs * this.options.reconnect.backoffFactor)
        );
      }
    }

    throw new ZlinkStreamException(lastError ?? { code: ZlinkStreamErrorCode.ConnectTimeout, message: 'Connect failed.' });
  }

  private startHeartbeat(): void {
    this.stopHeartbeat();
    if (!this.options.heartbeat.enabled) {
      return;
    }
    this.heartbeatTimer = setInterval(() => {
      void this.runHeartbeatTick();
    }, this.options.heartbeat.intervalMs);
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer !== undefined) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = undefined;
    }
  }

  private startReceiveLoop(): void {
    if (this.options.dispatchMode !== ZlinkStreamDispatchMode.Immediate || this.connection?.read === undefined) {
      return;
    }
    this.stopReceiveLoop();
    const abort = new AbortController();
    const connection = this.connection;
    this.receiveLoopAbort = abort;
    void this.runReceiveLoop(connection, abort.signal);
  }

  private stopReceiveLoop(): void {
    this.receiveLoopAbort?.abort();
    this.receiveLoopAbort = undefined;
  }

  private async runReceiveLoop(connection: ZlinkStreamConnection | undefined, signal: AbortSignal): Promise<void> {
    try {
      while (!signal.aborted && this.currentState === ZlinkStreamConnectionState.Connected && this.connection === connection) {
        const dispatched = await this.dispatchAvailable(signal);
        if (!dispatched && !signal.aborted && this.currentState === ZlinkStreamConnectionState.Connected && this.connection === connection) {
          await delay(1, signal);
        }
      }
    } catch (cause) {
      if (signal.aborted) {
        return;
      }
      const error = toStreamError(cause, ZlinkStreamErrorCode.FrameDecodeFailed, 'Receive loop failed.');
      this.stopHeartbeat();
      this.failPending(error);
      await this.connection?.close();
      this.connection = undefined;
      await this.setState(ZlinkStreamConnectionState.Disconnected, error);
    }
  }

  private async runHeartbeatTick(): Promise<void> {
    if (!this.isConnected) {
      return;
    }
    if (Date.now() - this.lastInboundAt > this.options.heartbeat.timeoutMs) {
      const error = { code: ZlinkStreamErrorCode.Disconnected, message: 'Heartbeat timed out.' };
      this.stopHeartbeat();
      await this.setState(ZlinkStreamConnectionState.Disconnected, error);
      this.failPending(error);
      await this.connection?.close();
      this.connection = undefined;
      return;
    }
    try {
      await this.sendControl(DefaultZlinkStreamConnector.heartbeatPingName);
    } catch (cause) {
      const error = toStreamError(cause, ZlinkStreamErrorCode.SendFailed, 'Heartbeat send failed.');
      this.stopHeartbeat();
      await this.setState(ZlinkStreamConnectionState.Disconnected, error);
    }
  }

  private trackPending(requestSeq: bigint, timeoutMs: number): PendingRequest {
    let timeout: NodeJS.Timeout | undefined;
    let resolvePending!: (value: ZlinkStreamEncodedPayload) => void;
    let rejectPending!: (error: ZlinkStreamError) => void;
    const promise = new Promise<ZlinkStreamEncodedPayload>((resolve, reject) => {
      timeout = setTimeout(() => {
        this.pendingRequests.delete(requestSeq);
        reject(connectorError(ZlinkStreamErrorCode.RequestTimeout, 'Request timed out.'));
      }, timeoutMs);
      resolvePending = resolve;
      rejectPending = (error) => reject(connectorError(error.code, error.message, error.cause));
    });
    const pending: PendingRequest = {
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
    };
    this.pendingRequests.set(requestSeq, pending);
    return pending;
  }

  private failPending(error: ZlinkStreamError): void {
    for (const [requestSeq, pending] of this.pendingRequests) {
      this.pendingRequests.delete(requestSeq);
      pending.reject(error);
    }
  }

  private async setState(current: ZlinkStreamConnectionState, error: ZlinkStreamError | undefined, signal?: AbortSignal): Promise<void> {
    const previous = this.currentState;
    this.currentState = current;
    if (previous === current && error === undefined) {
      return;
    }
    const change = { previous, current, error };
    await Promise.all([...this.stateHandlers].map((handler) => handler(change, signal)));
    if (error !== undefined) {
      await this.publishError(error, signal);
    }
  }

  private async publishError(error: ZlinkStreamError, signal?: AbortSignal): Promise<void> {
    await Promise.all([...this.errorHandlers].map((handler) => handler(error, signal)));
  }
}

interface PendingRequest {
  readonly promise: Promise<ZlinkStreamEncodedPayload>;
  resolve(value: ZlinkStreamEncodedPayload): void;
  reject(error: ZlinkStreamError): void;
  cancel(): void;
}
