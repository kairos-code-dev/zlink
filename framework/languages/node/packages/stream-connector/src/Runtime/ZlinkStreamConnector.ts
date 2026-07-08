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
  ZlinkStreamInboundObservation,
  zlinkStreamJsonCodec,
  ZlinkStreamMessage,
  ZlinkStreamMessageKind,
  ZlinkStreamMetadata,
  ZlinkStreamMetadataMap,
  ZlinkStreamRequestCall,
  ZlinkStreamSendCall,
  ZlinkStreamWaitCall
} from '../Contracts';
import { ZlinkStreamHeaderFlags } from '../Contracts/ZlinkStreamEnums';
import type { ZlinkStreamHeader } from '../Contracts/ZlinkStreamModels';
import { ZlinkStreamRequestBuilder, ZlinkStreamSendBuilder, ZlinkStreamWaitBuilder } from './Calls/ZlinkStreamCallBuilders';
import { buildHeader, ZlinkStreamHeaderCodec } from './Protocol/ZlinkStreamHeaderCodec';
import { ZlinkStreamFrameCodec } from './Protocol/ZlinkStreamFrameCodec';
import { validateName } from './Protocol/ZlinkStreamPacketNameValidator';
import { normalizeOptions } from './ZlinkStreamConnectorOptions';
import { connectorError, delay, subscription, throwIfAborted, toStreamError, utf8Decode } from './ZlinkStreamSupport';
import { compressPayload, decompressIfNeeded } from './Protocol/Compression/ZlinkStreamCompressionCodec';
import { ZlinkStreamPendingRequests } from './ZlinkStreamPendingRequests';
import { ZlinkStreamMessageHandlers } from './ZlinkStreamMessageHandlers';
import { ZlinkStreamInboundObservers } from './ZlinkStreamInboundObservers';

export const zlinkStreamConnectorFactory = {
  create(options: ZlinkStreamConnectorOptions): ZlinkStreamConnector {
    return new DefaultZlinkStreamConnector(options);
  }
};

export class DefaultZlinkStreamConnector implements ZlinkStreamConnector {
  static readonly heartbeatPingName = '$zlink.heartbeat.ping';
  static readonly heartbeatPongName = '$zlink.heartbeat.pong';

  private readonly handlers = new ZlinkStreamMessageHandlers();
  private readonly errorHandlers = new Set<(error: ZlinkStreamError, signal?: AbortSignal) => Promise<void> | void>();
  private readonly disconnectedHandlers = new Set<(signal?: AbortSignal) => Promise<void> | void>();
  private readonly stateHandlers = new Set<(change: ZlinkStreamConnectionStateChanged, signal?: AbortSignal) => Promise<void> | void>();
  private connection: ZlinkStreamConnection | undefined;
  private currentState = ZlinkStreamConnectionState.Created;
  private correlationCounter = 0n;
  private readonly pendingRequests = new ZlinkStreamPendingRequests();
  private readonly pendingWrites = new Set<Promise<void>>();
  private heartbeatTimer: NodeJS.Timeout | undefined;
  private receiveLoopAbort: AbortController | undefined;
  private lastInboundAt = 0;
  private readonly inboundObservers: ZlinkStreamInboundObservers;
  private readonly receivedMessages: Array<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> = [];
  private receivedMessageDrain: Promise<void> | undefined;
  private receivedMessageDropReportPending = false;

  readonly options: RequiredZlinkStreamConnectorOptions;

  constructor(options: ZlinkStreamConnectorOptions) {
    this.options = normalizeOptions(options);
    this.inboundObservers = new ZlinkStreamInboundObservers(
      this.options.maxInboundObserverNotifications,
      this.options.maxInboundObserverPayloadPreviewBytes,
      (error, signal) => this.publishError(error, signal)
    );
  }

  get isConnected(): boolean {
    return this.currentState === ZlinkStreamConnectionState.Connected;
  }

  get state(): ZlinkStreamConnectionState {
    return this.currentState;
  }

  get pendingDispatchCount(): number {
    return this.pendingRequests.count;
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
    this.stopHeartbeat();
    this.stopReceiveLoop();
    await this.drainPendingWrites(signal);
    this.connection = undefined;
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

  send(payload: unknown, messageType?: Function): ZlinkStreamSendCall {
    const encoded = this.encodePayload(payload, messageType);
    return new ZlinkStreamSendBuilder(this, this.resolveNameOrDefault(encoded), encoded);
  }

  request(payload: unknown, messageType?: Function): ZlinkStreamRequestCall {
    const encoded = this.encodePayload(payload, messageType);
    return new ZlinkStreamRequestBuilder(this, this.resolveNameOrDefault(encoded), encoded);
  }

  observeInbound(
    observer: (observation: ZlinkStreamInboundObservation, signal?: AbortSignal) => Promise<void> | void
  ): Disposable {
    if (this.currentState !== ZlinkStreamConnectionState.Created) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Inbound observers must be registered before connecting.');
    }
    return this.inboundObservers.add(observer);
  }

  on<TPayload = ZlinkStreamEncodedPayload>(
    name: string,
    handler: (message: ZlinkStreamMessage<TPayload>, signal?: AbortSignal) => Promise<void> | void,
    messageType?: Function
  ): Disposable {
    const encodedHandler = (message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal) => handler({
      name: message.name,
      metadata: message.metadata,
      payload: this.decodePayload<TPayload>(message.payload, messageType)
    }, signal);
    return this.handlers.on(name, encodedHandler);
  }

  waitFor<TPayload = ZlinkStreamEncodedPayload>(name: string): ZlinkStreamWaitCall<TPayload> {
    validateName(name);
    return new ZlinkStreamWaitBuilder<TPayload>(this, name);
  }

  waitForMessage<TPayload>(
    name: string,
    timeoutMs: number,
    predicate: (message: ZlinkStreamMessage<TPayload>) => boolean,
    signal?: AbortSignal
  ): Promise<ZlinkStreamMessage<TPayload>> {
    validateName(name);
    if (!Number.isFinite(timeoutMs) || timeoutMs < 0) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Timeout must be a non-negative finite number.');
    }
    throwIfAborted(signal);
    return new Promise((resolve, reject) => {
      let done = false;
      let timer: NodeJS.Timeout | undefined;
      let disposable: Disposable | undefined;
      const onAbort = () => finish(connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.'));
      const finish = (error?: unknown, message?: ZlinkStreamMessage<TPayload>) => {
        if (done) {
          return;
        }
        done = true;
        signal?.removeEventListener('abort', onAbort);
        if (timer !== undefined) {
          clearTimeout(timer);
        }
        disposable?.dispose();
        if (error !== undefined) {
          reject(error);
        } else {
          resolve(message!);
        }
      };
      timer = setTimeout(() => {
        finish(connectorError(ZlinkStreamErrorCode.RequestTimeout, 'Wait for stream message timed out.'));
      }, timeoutMs);
      signal?.addEventListener('abort', onAbort, { once: true });
      disposable = this.handlers.on(name, (message) => {
        try {
          const decoded = {
            name: message.name,
            metadata: message.metadata,
            payload: this.decodeWaitPayload<TPayload>(message.payload)
          };
          if (predicate(decoded)) {
            finish(undefined, decoded);
          }
        } catch (cause) {
          finish(cause);
        }
      });
    });
  }

  private encodePayload(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
    if (isEncodedPayload(payload)) {
      return payload;
    }
    const codec = this.options.codec ?? zlinkStreamJsonCodec;
    return codec.encode(payload, messageType);
  }

  private decodePayload<TPayload>(payload: ZlinkStreamEncodedPayload, messageType?: Function): TPayload {
    if (messageType === undefined && this.options.codec === undefined) {
      return payload as TPayload;
    }
    return (this.options.codec ?? zlinkStreamJsonCodec).decode<TPayload>(payload, messageType);
  }

  private decodeWaitPayload<TPayload>(payload: ZlinkStreamEncodedPayload): TPayload {
    if (this.options.codec !== undefined || payload.codec === ZlinkStreamCodec.Json) {
      return (this.options.codec ?? zlinkStreamJsonCodec).decode<TPayload>(payload);
    }
    return payload as TPayload;
  }

  async sendEncoded(
    kind: ZlinkStreamMessageKind,
    name: string,
    payload: ZlinkStreamEncodedPayload,
    metadata: ZlinkStreamMetadata,
    compress: boolean,
    requestSeq: bigint | undefined,
    signal?: AbortSignal,
    correlationId?: string
  ): Promise<void> {
    throwIfAborted(signal);
    const payloadBytes = compress
      ? compressPayload(payload.payload, this.options.compression, this.options.compressionCodec)
      : payload.payload;
    const header = buildHeader(kind, name, payload.codec, metadata, compress, requestSeq, correlationId);
    await this.sendFrame(header, payloadBytes, signal);
  }

  /**
   * Per-connector monotonic correlation id (hex). The client generates it on each request
   * and the server echoes it back on the reply, so flows can be joined across the wire.
   */
  private nextCorrelationId(): string {
    this.correlationCounter += 1n;
    return this.correlationCounter.toString(16);
  }

  async requestEncoded(
    name: string,
    payload: ZlinkStreamEncodedPayload,
    metadata: ZlinkStreamMetadata,
    compress: boolean,
    timeoutMs: number,
    signal?: AbortSignal
  ): Promise<ZlinkStreamEncodedPayload> {
    const pending = this.pendingRequests.create(timeoutMs);
    try {
      await this.sendEncoded(
        ZlinkStreamMessageKind.Request,
        name,
        payload,
        metadata,
        compress,
        pending.requestSeq,
        signal,
        this.nextCorrelationId()
      );
      return await pending.promise;
    } catch (error) {
      this.pendingRequests.cancel(pending.requestSeq);
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
    const write = connection.write(frame, signal);
    this.pendingWrites.add(write);
    try {
      await write;
    } finally {
      this.pendingWrites.delete(write);
    }
  }

  private async drainPendingWrites(signal?: AbortSignal): Promise<void> {
    while (this.pendingWrites.size > 0) {
      throwIfAborted(signal);
      await Promise.allSettled([...this.pendingWrites]);
    }
  }

  private resolveNameOrDefault(payload: ZlinkStreamEncodedPayload): string | undefined {
    if (payload.messageType === undefined) {
      return undefined;
    }
    return this.options.nameResolver.resolve(payload.messageType);
  }

  private async dispatchFrame(header: ZlinkStreamHeader, payload: Uint8Array, signal?: AbortSignal): Promise<void> {
    this.inboundObservers.enqueue(header, payload, signal);
    if (header.kind === ZlinkStreamMessageKind.Response && header.requestSeq !== undefined) {
      try {
        this.pendingRequests.resolve(header.requestSeq, { codec: header.codec, payload: this.payloadForHeader(header, payload) });
      } catch (cause) {
        this.pendingRequests.reject(header.requestSeq, toStreamError(cause, ZlinkStreamErrorCode.DecompressionFailed, 'Decompression failed.'));
      }
      return;
    }
    if (header.kind === ZlinkStreamMessageKind.Error && header.requestSeq !== undefined) {
      this.pendingRequests.reject(header.requestSeq, { code: ZlinkStreamErrorCode.RemoteError, message: utf8Decode(payload) });
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
      this.enqueueReceivedMessage({
        name: header.name,
        metadata: header.metadata,
        payload: { codec: header.codec, payload: this.payloadForHeader(header, payload) }
      }, signal);
    }
  }

  private enqueueReceivedMessage(message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal): void {
    if (this.receivedMessages.length >= this.options.maxReceivedMessages) {
      this.reportReceivedMessageDropped(signal);
      return;
    }
    this.receivedMessages.push(message);
    this.scheduleReceivedMessageDrain(signal);
  }

  private scheduleReceivedMessageDrain(signal?: AbortSignal): void {
    if (this.receivedMessageDrain !== undefined) {
      return;
    }
    this.receivedMessageDrain = this.drainReceivedMessages(signal).finally(() => {
      this.receivedMessageDrain = undefined;
      if (this.receivedMessages.length > 0) {
        this.scheduleReceivedMessageDrain(signal);
      }
    });
  }

  private async drainReceivedMessages(signal?: AbortSignal): Promise<void> {
    while (this.receivedMessages.length > 0) {
      const message = this.receivedMessages.shift()!;
      await this.handlers.dispatch(message, signal, (error, handlerSignal) => this.publishError(error, handlerSignal));
    }
  }

  private reportReceivedMessageDropped(signal?: AbortSignal): void {
    if (this.receivedMessageDropReportPending) {
      return;
    }
    this.receivedMessageDropReportPending = true;
    queueMicrotask(() => {
      void this.publishError({
        code: ZlinkStreamErrorCode.ReceivedMessageDropped,
        message: 'Received stream message was dropped because the received-message queue is full.'
      }, signal).finally(() => {
        this.receivedMessageDropReportPending = false;
      }).catch(() => {});
    });
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
    return decompressIfNeeded(
      header,
      payload,
      this.options.compression,
      this.options.compressionCodec,
      this.options.maxReceivePayloadSize
    );
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
      while (this.shouldContinueReceiveLoop(connection, signal)) {
        const dispatched = await this.dispatchAvailable(signal);
        if (!dispatched && this.shouldContinueReceiveLoop(connection, signal)) {
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

  private shouldContinueReceiveLoop(connection: ZlinkStreamConnection | undefined, signal: AbortSignal): boolean {
    return !signal.aborted
      && this.currentState === ZlinkStreamConnectionState.Connected
      && this.connection === connection;
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

  private failPending(error: ZlinkStreamError): void {
    this.pendingRequests.failAll(error);
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

function isEncodedPayload(value: unknown): value is ZlinkStreamEncodedPayload {
  if (value === null || typeof value !== 'object') {
    return false;
  }
  const candidate = value as Partial<ZlinkStreamEncodedPayload>;
  return typeof candidate.codec === 'number' && candidate.payload instanceof Uint8Array;
}
