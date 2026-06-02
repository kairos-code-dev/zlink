import * as net from 'node:net';
import * as tls from 'node:tls';

export enum ZlinkStreamTransport {
  Tcp = 'tcp',
  Tls = 'tls',
  WebSocket = 'webSocket',
  WebSocketSecure = 'webSocketSecure'
}

export enum ZlinkStreamCodec {
  Raw = 0,
  Json = 1,
  MessagePack = 2,
  Protobuf = 3
}

export enum ZlinkStreamCompression {
  None = 'none',
  Lz4 = 'lz4'
}

export enum ZlinkStreamDispatchMode {
  Manual = 'manual',
  Immediate = 'immediate'
}

export enum ZlinkStreamMessageKind {
  Send = 1,
  Request = 2,
  Response = 3,
  Error = 4,
  Control = 5
}

export enum ZlinkStreamHeaderFlags {
  None = 0,
  HasRequestSeq = 0x01,
  HasMetadata = 0x02,
  PayloadCompressed = 0x04
}

export enum ZlinkStreamErrorCode {
  Disconnected = 'disconnected',
  ConfigurationError = 'configurationError',
  ValidationFailed = 'validationFailed',
  RequestTimeout = 'requestTimeout',
  ConnectTimeout = 'connectTimeout',
  FrameDecodeFailed = 'frameDecodeFailed',
  FrameTooLarge = 'frameTooLarge',
  SendFailed = 'sendFailed',
  CompressionFailed = 'compressionFailed',
  TlsValidationFailed = 'tlsValidationFailed',
  DecompressionFailed = 'decompressionFailed',
  UserCallbackFailed = 'userCallbackFailed',
  RemoteError = 'remoteError'
}

export enum ZlinkStreamConnectionState {
  Created = 'created',
  Connecting = 'connecting',
  Connected = 'connected',
  Reconnecting = 'reconnecting',
  Disconnected = 'disconnected',
  Closed = 'closed'
}

export interface ZlinkStreamConnectorOptions {
  readonly endpoint: string;
  readonly transport?: ZlinkStreamTransport;
  readonly transportFactory?: ZlinkStreamTransportFactory;
  readonly connectTimeoutMs?: number;
  readonly requestTimeoutMs?: number;
  readonly heartbeat?: ZlinkStreamHeartbeatOptions;
  readonly reconnect?: ZlinkStreamReconnectOptions;
  readonly maxSendPayloadSize?: number;
  readonly skipServerCertificateValidation?: boolean;
  readonly dispatchMode?: ZlinkStreamDispatchMode;
  readonly compression?: ZlinkStreamCompression;
  readonly nameResolver?: ZlinkStreamPacketNameResolver;
}

export interface ZlinkStreamHeartbeatOptions {
  readonly enabled?: boolean;
  readonly intervalMs?: number;
  readonly timeoutMs?: number;
}

export interface ZlinkStreamReconnectOptions {
  readonly enabled?: boolean;
  readonly initialDelayMs?: number;
  readonly maxDelayMs?: number;
  readonly backoffFactor?: number;
  readonly maxAttempts?: number;
}

export interface ZlinkStreamPacketNameResolver {
  resolve(payloadType: Function): string;
}

export interface ZlinkStreamTransportFactory {
  connect(options: RequiredZlinkStreamConnectorOptions, signal?: AbortSignal): Promise<ZlinkStreamConnection>;
}

export interface ZlinkStreamConnection {
  write(frame: Uint8Array, signal?: AbortSignal): Promise<void>;
  read?(signal?: AbortSignal): Promise<Uint8Array | undefined>;
  close(signal?: AbortSignal): Promise<void>;
}

export interface RequiredZlinkStreamConnectorOptions {
  readonly endpoint: string;
  readonly transport: ZlinkStreamTransport;
  readonly connectTimeoutMs: number;
  readonly requestTimeoutMs: number;
  readonly heartbeat: Required<ZlinkStreamHeartbeatOptions>;
  readonly reconnect: Required<ZlinkStreamReconnectOptions>;
  readonly maxSendPayloadSize: number;
  readonly skipServerCertificateValidation: boolean;
  readonly dispatchMode: ZlinkStreamDispatchMode;
  readonly compression: ZlinkStreamCompression;
  readonly nameResolver: ZlinkStreamPacketNameResolver;
  readonly transportFactory: ZlinkStreamTransportFactory;
}

export interface ZlinkStreamEncodedPayload {
  readonly codec: ZlinkStreamCodec;
  readonly payload: Uint8Array;
  readonly messageType?: Function;
}

export interface ZlinkStreamMetadata {
  readonly count: number;
  readonly values: ReadonlyMap<string, string>;
  get(key: string): string | undefined;
  with(key: string, value: string): ZlinkStreamMetadata;
  withMany(values: Iterable<readonly [string, string]>): ZlinkStreamMetadata;
}

export interface ZlinkStreamMessage<TPayload = unknown> {
  readonly name: string;
  readonly metadata: ZlinkStreamMetadata;
  readonly payload: TPayload;
}

export interface ZlinkStreamHeader {
  readonly kind: ZlinkStreamMessageKind;
  readonly codec: ZlinkStreamCodec;
  readonly flags: ZlinkStreamHeaderFlags;
  readonly requestSeq?: bigint;
  readonly name: string;
  readonly metadata: ZlinkStreamMetadata;
}

export interface ZlinkStreamError {
  readonly code: ZlinkStreamErrorCode;
  readonly message: string;
  readonly cause?: unknown;
}

export interface ZlinkStreamConnectionStateChanged {
  readonly previous: ZlinkStreamConnectionState;
  readonly current: ZlinkStreamConnectionState;
  readonly error?: ZlinkStreamError;
}

export interface ZlinkStreamResult {
  readonly isSuccess: boolean;
  readonly error?: ZlinkStreamError;
}

export interface ZlinkStreamResultOf<T> extends ZlinkStreamResult {
  readonly value?: T;
}

export interface ZlinkStreamSendCall {
  packetName(name: string): ZlinkStreamSendCall;
  metadata(key: string, value: string): ZlinkStreamSendCall;
  metadata(metadata: ZlinkStreamMetadata): ZlinkStreamSendCall;
  compress(): ZlinkStreamSendCall;
  submit(signal?: AbortSignal): Promise<void>;
}

export interface ZlinkStreamRequestCall {
  packetName(name: string): ZlinkStreamRequestCall;
  metadata(key: string, value: string): ZlinkStreamRequestCall;
  metadata(metadata: ZlinkStreamMetadata): ZlinkStreamRequestCall;
  timeout(timeoutMs: number): ZlinkStreamRequestCall;
  compress(): ZlinkStreamRequestCall;
  submit(signal?: AbortSignal): Promise<ZlinkStreamEncodedPayload>;
  submit(callback: (result: ZlinkStreamResultOf<ZlinkStreamEncodedPayload>) => void): void;
}

export interface ZlinkStreamConnector {
  readonly isConnected: boolean;
  readonly state: ZlinkStreamConnectionState;
  readonly options: RequiredZlinkStreamConnectorOptions;
  readonly pendingDispatchCount: number;
  onErrorReceived(handler: (error: ZlinkStreamError, signal?: AbortSignal) => Promise<void> | void): Disposable;
  onDisconnected(handler: (signal?: AbortSignal) => Promise<void> | void): Disposable;
  onConnectionStateChanged(handler: (change: ZlinkStreamConnectionStateChanged, signal?: AbortSignal) => Promise<void> | void): Disposable;
  connect(signal?: AbortSignal): Promise<void>;
  close(signal?: AbortSignal): Promise<void>;
  dispatch(signal?: AbortSignal): Promise<void>;
  send(payload: ZlinkStreamEncodedPayload): ZlinkStreamSendCall;
  request(payload: ZlinkStreamEncodedPayload): ZlinkStreamRequestCall;
  on(name: string, handler: (message: ZlinkStreamMessage<ZlinkStreamEncodedPayload>, signal?: AbortSignal) => Promise<void> | void): Disposable;
}

export interface Disposable {
  dispose(): void;
}

export class ZlinkStreamException extends Error {
  constructor(readonly error: ZlinkStreamError) {
    super(error.message);
    this.name = 'ZlinkStreamException';
  }
}

export class ZlinkStreamMetadataMap implements ZlinkStreamMetadata {
  static readonly empty: ZlinkStreamMetadata = new ZlinkStreamMetadataMap(new Map());

  private constructor(readonly values: ReadonlyMap<string, string>) {}

  get count(): number {
    return this.values.size;
  }

  get(key: string): string | undefined {
    return this.values.get(key);
  }

  with(key: string, value: string): ZlinkStreamMetadata {
    validateMetadataKey(key);
    const next = new Map(this.values);
    next.set(key, value);
    return new ZlinkStreamMetadataMap(next);
  }

  withMany(values: Iterable<readonly [string, string]>): ZlinkStreamMetadata {
    const next = new Map(this.values);
    for (const [key, value] of values) {
      validateMetadataKey(key);
      next.set(key, value);
    }
    return new ZlinkStreamMetadataMap(next);
  }

  static from(values: Iterable<readonly [string, string]>): ZlinkStreamMetadata {
    return ZlinkStreamMetadataMap.empty.withMany(values);
  }
}

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
    if (connection !== undefined) {
      await connection.close(signal);
    }
    this.failPending({ code: ZlinkStreamErrorCode.Disconnected, message: 'Connector closed.' });
    await this.setState(ZlinkStreamConnectionState.Closed, undefined, signal);
    await Promise.all([...this.disconnectedHandlers].map((handler) => handler(signal)));
  }

  async dispatch(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const connection = this.connection;
    if (connection?.read === undefined) {
      return;
    }
    const frameBytes = await connection.read(signal);
    if (frameBytes === undefined) {
      return;
    }
    const frame = ZlinkStreamFrameCodec.decode(frameBytes);
    const header = ZlinkStreamHeaderCodec.decode(frame.header);
    this.lastInboundAt = Date.now();
    await this.dispatchFrame(header, frame.payload, signal);
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
    const header = buildHeader(kind, name, payload.codec, metadata, compress, requestSeq);
    await this.sendFrame(header, payload.payload, signal);
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
        pending.resolve({ codec: header.codec, payload });
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
    if (header.kind === ZlinkStreamMessageKind.Control) {
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
        const message = { name: header.name, metadata: header.metadata, payload: { codec: header.codec, payload } };
        await Promise.all([...handlers].map((handler) => handler(message, signal)));
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
      await Promise.all([...this.errorHandlers].map((handler) => handler(error, signal)));
    }
  }
}

interface PendingRequest {
  readonly promise: Promise<ZlinkStreamEncodedPayload>;
  resolve(value: ZlinkStreamEncodedPayload): void;
  reject(error: ZlinkStreamError): void;
  cancel(): void;
}

export class ZlinkStreamHeaderCodec {
  static encode(header: ZlinkStreamHeader): Uint8Array {
    validateName(header.name, header.kind === ZlinkStreamMessageKind.Control);
    validateHeaderSemantics(header);

    const nameBytes = utf8Encode(header.name);
    const hasRequestSeq = header.requestSeq !== undefined;
    const hasMetadata = header.metadata.count > 0;
    let flags = header.flags;
    flags = hasRequestSeq ? flags | ZlinkStreamHeaderFlags.HasRequestSeq : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
    flags = hasMetadata ? flags | ZlinkStreamHeaderFlags.HasMetadata : flags & ~ZlinkStreamHeaderFlags.HasMetadata;

    const metadataSize = hasMetadata ? ZlinkStreamMetadataCodec.size(header.metadata) : 0;
    const size = 3 + (hasRequestSeq ? 8 : 0) + 1 + nameBytes.length + (hasMetadata ? 2 + metadataSize : 0);
    const buffer = new Uint8Array(size);
    let offset = 0;
    buffer[offset++] = header.kind;
    buffer[offset++] = header.codec;
    buffer[offset++] = flags;
    if (hasRequestSeq) {
      if (header.requestSeq === 0n) {
        throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Request sequence must not be zero.');
      }
      writeBigUInt64BE(buffer, offset, header.requestSeq);
      offset += 8;
    }
    buffer[offset++] = nameBytes.length;
    buffer.set(nameBytes, offset);
    offset += nameBytes.length;
    if (hasMetadata) {
      writeUInt16BE(buffer, offset, metadataSize);
      offset += 2;
      ZlinkStreamMetadataCodec.write(header.metadata, buffer.subarray(offset, offset + metadataSize));
    }
    return buffer;
  }

  static decode(header: Uint8Array): ZlinkStreamHeader {
    if (header.length < 4) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header is too short.');
    }
    let offset = 0;
    const kind = header[offset++] as ZlinkStreamMessageKind;
    const codec = header[offset++] as ZlinkStreamCodec;
    const flags = header[offset++] as ZlinkStreamHeaderFlags;
    validateEnum(kind, codec, flags);

    let requestSeq: bigint | undefined;
    if ((flags & ZlinkStreamHeaderFlags.HasRequestSeq) !== 0) {
      if (header.length - offset < 8) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header request sequence is incomplete.');
      }
      requestSeq = readBigUInt64BE(header, offset);
      if (requestSeq === 0n) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Request sequence must not be zero.');
      }
      offset += 8;
    }

    if (header.length - offset < 1) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header name length is missing.');
    }
    const nameLength = header[offset++];
    if (nameLength === 0 || header.length - offset < nameLength) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header packet name is invalid.');
    }
    const name = utf8Decode(header.subarray(offset, offset + nameLength));
    offset += nameLength;

    let metadata = ZlinkStreamMetadataMap.empty;
    if ((flags & ZlinkStreamHeaderFlags.HasMetadata) !== 0) {
      if (header.length - offset < 2) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header metadata length is missing.');
      }
      const metadataLength = readUInt16BE(header, offset);
      offset += 2;
      if (header.length - offset < metadataLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header metadata is incomplete.');
      }
      metadata = ZlinkStreamMetadataCodec.decode(header.subarray(offset, offset + metadataLength));
      offset += metadataLength;
    }
    if (offset !== header.length) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Helper header contains trailing bytes.');
    }

    const decoded = { kind, codec, flags, requestSeq, name, metadata };
    validateName(name, kind === ZlinkStreamMessageKind.Control);
    validateHeaderSemantics(decoded);
    return decoded;
  }
}

export class ZlinkStreamFrameCodec {
  static encode(header: Uint8Array, payload: Uint8Array, maxPayloadSize = 64 * 1024): Uint8Array {
    validatePayload(payload.length, maxPayloadSize);
    validateFrame(header.length, payload.length);
    const frame = new Uint8Array(6 + header.length + payload.length);
    writeUInt16BE(frame, 0, header.length);
    writeUInt32BE(frame, 2, payload.length);
    frame.set(header, 6);
    frame.set(payload, 6 + header.length);
    return frame;
  }

  static decode(frame: Uint8Array): { header: Uint8Array; payload: Uint8Array } {
    if (frame.length < 6) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame prefix is incomplete.');
    }
    const headerLength = readUInt16BE(frame, 0);
    const payloadLength = readUInt32BE(frame, 2);
    const expected = 6 + headerLength + payloadLength;
    if (frame.length !== expected) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Frame length does not match prefix.');
    }
    return {
      header: frame.slice(6, 6 + headerLength),
      payload: frame.slice(6 + headerLength)
    };
  }
}

class ZlinkStreamMetadataCodec {
  static size(metadata: ZlinkStreamMetadata): number {
    if (metadata.count === 0) {
      return 0;
    }
    if (metadata.count > 255) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata entry count must not exceed 255.');
    }
    let size = 1;
    for (const [key, value] of metadata.values) {
      const keyLength = utf8Encode(key).length;
      const valueLength = utf8Encode(value).length;
      if (keyLength === 0 || keyLength > 255) {
        throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata key length is invalid.');
      }
      if (valueLength > 0xffff) {
        throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata value is too large.');
      }
      size += 1 + keyLength + 2 + valueLength;
    }
    return size;
  }

  static write(metadata: ZlinkStreamMetadata, destination: Uint8Array): void {
    let offset = 0;
    destination[offset++] = metadata.count;
    for (const [key, value] of metadata.values) {
      const keyBytes = utf8Encode(key);
      const valueBytes = utf8Encode(value);
      destination[offset++] = keyBytes.length;
      destination.set(keyBytes, offset);
      offset += keyBytes.length;
      writeUInt16BE(destination, offset, valueBytes.length);
      offset += 2;
      destination.set(valueBytes, offset);
      offset += valueBytes.length;
    }
  }

  static decode(metadata: Uint8Array): ZlinkStreamMetadata {
    if (metadata.length === 0) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata payload is empty.');
    }
    let offset = 0;
    const count = metadata[offset++];
    const values = new Map<string, string>();
    for (let i = 0; i < count; i++) {
      const keyLength = readLength(metadata, offset, 1, 'key');
      offset += 1;
      if (keyLength === 0 || metadata.length - offset < keyLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata key is invalid.');
      }
      const key = utf8Decode(metadata.subarray(offset, offset + keyLength));
      offset += keyLength;
      const valueLength = readLength(metadata, offset, 2, 'value');
      offset += 2;
      if (metadata.length - offset < valueLength) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata value is invalid.');
      }
      if (values.has(key)) {
        throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Duplicate metadata key.');
      }
      values.set(key, utf8Decode(metadata.subarray(offset, offset + valueLength)));
      offset += valueLength;
    }
    if (offset !== metadata.length) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Metadata contains trailing bytes.');
    }
    return values.size === 0 ? ZlinkStreamMetadataMap.empty : ZlinkStreamMetadataMap.from(values);
  }
}

class ZlinkStreamCallBuilderState {
  private executed = false;
  name: string | undefined;
  metadata: ZlinkStreamMetadata = ZlinkStreamMetadataMap.empty;
  timeoutMs: number | undefined;
  compress = false;

  constructor(name: string | undefined) {
    this.name = name;
  }

  ensureNotExecuted(): void {
    if (this.executed) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Builder instances can be executed only once.');
    }
    this.executed = true;
  }

  resolveMessageName(): string {
    if (this.name === undefined) {
      throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name is required when the encoded stream payload has no message type.');
    }
    return this.name;
  }
}

class ZlinkStreamSendBuilder implements ZlinkStreamSendCall {
  private readonly state: ZlinkStreamCallBuilderState;

  constructor(
    private readonly connector: DefaultZlinkStreamConnector,
    name: string | undefined,
    private readonly payload: ZlinkStreamEncodedPayload
  ) {
    this.state = new ZlinkStreamCallBuilderState(name);
  }

  packetName(name: string): this {
    validateName(name);
    this.state.name = name;
    return this;
  }

  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  metadata(keyOrMetadata: string | ZlinkStreamMetadata, value?: string): this {
    this.state.metadata = typeof keyOrMetadata === 'string'
      ? this.state.metadata.with(keyOrMetadata, value ?? '')
      : keyOrMetadata;
    return this;
  }

  compress(): this {
    this.state.compress = true;
    return this;
  }

  submit(signal?: AbortSignal): Promise<void> {
    this.state.ensureNotExecuted();
    return this.connector.sendEncoded(
      ZlinkStreamMessageKind.Send,
      this.state.resolveMessageName(),
      this.payload,
      this.state.metadata,
      this.state.compress,
      undefined,
      signal
    );
  }
}

class ZlinkStreamRequestBuilder implements ZlinkStreamRequestCall {
  private readonly state: ZlinkStreamCallBuilderState;

  constructor(
    private readonly connector: DefaultZlinkStreamConnector,
    name: string | undefined,
    private readonly payload: ZlinkStreamEncodedPayload
  ) {
    this.state = new ZlinkStreamCallBuilderState(name);
  }

  packetName(name: string): this {
    validateName(name);
    this.state.name = name;
    return this;
  }

  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  metadata(keyOrMetadata: string | ZlinkStreamMetadata, value?: string): this {
    this.state.metadata = typeof keyOrMetadata === 'string'
      ? this.state.metadata.with(keyOrMetadata, value ?? '')
      : keyOrMetadata;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.state.timeoutMs = timeoutMs;
    return this;
  }

  compress(): this {
    this.state.compress = true;
    return this;
  }

  submit(signal?: AbortSignal): Promise<ZlinkStreamEncodedPayload>;
  submit(callback: (result: ZlinkStreamResultOf<ZlinkStreamEncodedPayload>) => void): void;
  submit(signalOrCallback?: AbortSignal | ((result: ZlinkStreamResultOf<ZlinkStreamEncodedPayload>) => void)): Promise<ZlinkStreamEncodedPayload> | void {
    this.state.ensureNotExecuted();
    const operation = this.connector.requestEncoded(
      this.state.resolveMessageName(),
      this.payload,
      this.state.metadata,
      this.state.compress,
      this.state.timeoutMs ?? this.connector.options.requestTimeoutMs,
      typeof signalOrCallback === 'function' ? undefined : signalOrCallback
    );
    if (typeof signalOrCallback === 'function') {
      operation.then(
        (value) => signalOrCallback({ isSuccess: true, value }),
        (error) => signalOrCallback({ isSuccess: false, error: unwrapStreamError(error) })
      );
      return;
    }
    return operation;
  }
}

class NodeStreamTransportFactory implements ZlinkStreamTransportFactory {
  async connect(options: RequiredZlinkStreamConnectorOptions, signal?: AbortSignal): Promise<ZlinkStreamConnection> {
    if (options.transport === ZlinkStreamTransport.WebSocket || options.transport === ZlinkStreamTransport.WebSocketSecure) {
      throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'WebSocket stream transport is not implemented yet.');
    }

    const endpoint = parseEndpoint(options.endpoint);
    const socket = options.transport === ZlinkStreamTransport.Tls
      ? await connectTls(endpoint, options, signal)
      : await connectTcp(endpoint, options.connectTimeoutMs, signal);
    return new NodeDuplexStreamConnection(socket);
  }
}

function normalizeOptions(options: ZlinkStreamConnectorOptions): RequiredZlinkStreamConnectorOptions {
  const endpoint = options.endpoint;
  if (endpoint.trim().length === 0) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint must not be empty.');
  }
  const inferredTransport = inferTransport(endpoint);
  if (options.transport !== undefined && options.transport !== inferredTransport) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Configured transport conflicts with endpoint scheme.');
  }
  validatePositive(options.connectTimeoutMs ?? 5000, 'ConnectTimeout');
  validatePositive(options.requestTimeoutMs ?? 30000, 'RequestTimeout');
  validatePositive(options.maxSendPayloadSize ?? 64 * 1024, 'MaxSendPayloadSize');
  validateHeartbeat(options.heartbeat);
  validateReconnect(options.reconnect);

  return {
    endpoint,
    transport: inferredTransport,
    connectTimeoutMs: options.connectTimeoutMs ?? 5000,
    requestTimeoutMs: options.requestTimeoutMs ?? 30000,
    heartbeat: {
      enabled: options.heartbeat?.enabled ?? true,
      intervalMs: options.heartbeat?.intervalMs ?? 1000,
      timeoutMs: options.heartbeat?.timeoutMs ?? 5000
    },
    reconnect: {
      enabled: options.reconnect?.enabled ?? true,
      initialDelayMs: options.reconnect?.initialDelayMs ?? 250,
      maxDelayMs: options.reconnect?.maxDelayMs ?? 5000,
      backoffFactor: options.reconnect?.backoffFactor ?? 2.0,
      maxAttempts: options.reconnect?.maxAttempts ?? 3
    },
    maxSendPayloadSize: options.maxSendPayloadSize ?? 64 * 1024,
    skipServerCertificateValidation: options.skipServerCertificateValidation ?? false,
    dispatchMode: options.dispatchMode ?? ZlinkStreamDispatchMode.Manual,
    compression: options.compression ?? ZlinkStreamCompression.None,
    nameResolver: options.nameResolver ?? { resolve: (type) => type.name },
    transportFactory: options.transportFactory ?? new NodeStreamTransportFactory()
  };
}

function buildHeader(
  kind: ZlinkStreamMessageKind,
  name: string,
  codec: ZlinkStreamCodec,
  metadata: ZlinkStreamMetadata,
  compress: boolean,
  requestSeq: bigint | undefined
): ZlinkStreamHeader {
  let flags = ZlinkStreamHeaderFlags.None;
  if (requestSeq !== undefined) {
    flags |= ZlinkStreamHeaderFlags.HasRequestSeq;
  }
  if (metadata.count > 0) {
    flags |= ZlinkStreamHeaderFlags.HasMetadata;
  }
  if (compress) {
    flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
  }
  return { kind, codec, flags, requestSeq, name, metadata };
}

function validateName(name: string, allowReserved = false): void {
  if (name.length === 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name must not be empty.');
  }
  if (!allowReserved && name.startsWith('$zlink.')) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name uses a reserved zlink prefix.');
  }
  if (utf8Encode(name).length > 255) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Message name must not exceed 255 UTF-8 bytes.');
  }
}

function validateMetadataKey(key: string): void {
  if (key.length === 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Metadata key must not be empty.');
  }
}

function validateHeaderSemantics(header: ZlinkStreamHeader): void {
  validateEnum(header.kind, header.codec, header.flags);
  const hasRequestSeq = header.requestSeq !== undefined || (header.flags & ZlinkStreamHeaderFlags.HasRequestSeq) !== 0;
  const hasMetadata = header.metadata.count > 0 || (header.flags & ZlinkStreamHeaderFlags.HasMetadata) !== 0;
  if (header.kind === ZlinkStreamMessageKind.Send && hasRequestSeq) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Send packet must not contain a request sequence.');
  }
  if ((header.kind === ZlinkStreamMessageKind.Request || header.kind === ZlinkStreamMessageKind.Response) && !hasRequestSeq) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Request and response packets must contain a request sequence.');
  }
  if (header.kind === ZlinkStreamMessageKind.Error && header.codec !== ZlinkStreamCodec.Json) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Error packet must use the JSON codec.');
  }
  if (header.kind === ZlinkStreamMessageKind.Control) {
    if (header.flags !== ZlinkStreamHeaderFlags.None || hasRequestSeq || hasMetadata || header.codec !== ZlinkStreamCodec.Raw) {
      throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Control packet must use raw codec and must not contain flags.');
    }
  }
}

function validateEnum(kind: ZlinkStreamMessageKind, codec: ZlinkStreamCodec, flags: ZlinkStreamHeaderFlags): void {
  if (![1, 2, 3, 4, 5].includes(kind)) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream message kind.');
  }
  if (![0, 1, 2, 3].includes(codec)) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream codec.');
  }
  const known = ZlinkStreamHeaderFlags.HasRequestSeq | ZlinkStreamHeaderFlags.HasMetadata | ZlinkStreamHeaderFlags.PayloadCompressed;
  if ((flags & ~known) !== 0) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, 'Unknown stream header flag.');
  }
}

function validateFrame(headerLength: number, payloadLength: number): void {
  if (headerLength > 0xffff) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Header exceeds u16 header_size.');
  }
  if (2 + 4 + headerLength + payloadLength > Number.MAX_SAFE_INTEGER) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Frame is too large.');
  }
}

function validatePayload(payloadLength: number, maxPayloadSize: number): void {
  if (payloadLength > maxPayloadSize) {
    throw connectorError(ZlinkStreamErrorCode.FrameTooLarge, 'Payload exceeds MaxSendPayloadSize.');
  }
}

function inferTransport(endpoint: string): ZlinkStreamTransport {
  const url = parseEndpoint(endpoint);
  switch (url.protocol) {
    case 'tcp:':
      return ZlinkStreamTransport.Tcp;
    case 'tls:':
      return ZlinkStreamTransport.Tls;
    case 'ws:':
      return ZlinkStreamTransport.WebSocket;
    case 'wss:':
      return ZlinkStreamTransport.WebSocketSecure;
    default:
      throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint scheme is not supported.');
  }
}

class NodeDuplexStreamConnection implements ZlinkStreamConnection {
  private readonly chunks: Buffer[] = [];
  private bufferedBytes = 0;
  private closed = false;
  private readWaiter: (() => void) | undefined;
  private error: Error | undefined;

  constructor(private readonly socket: net.Socket | tls.TLSSocket) {
    socket.on('data', (chunk: Buffer) => {
      this.chunks.push(chunk);
      this.bufferedBytes += chunk.length;
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
    if (this.bufferedBytes < 6) {
      return undefined;
    }
    const prefix = this.peek(6);
    const headerLength = readUInt16BE(prefix, 0);
    const payloadLength = readUInt32BE(prefix, 2);
    const frameLength = 6 + headerLength + payloadLength;
    if (this.bufferedBytes < frameLength) {
      return undefined;
    }
    return this.consume(frameLength);
  }

  private peek(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    for (const chunk of this.chunks) {
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      if (offset === length) {
        break;
      }
    }
    return output;
  }

  private consume(length: number): Uint8Array {
    const output = new Uint8Array(length);
    let offset = 0;
    while (offset < length) {
      const chunk = this.chunks[0];
      const take = Math.min(chunk.length, length - offset);
      output.set(chunk.subarray(0, take), offset);
      offset += take;
      this.bufferedBytes -= take;
      if (take === chunk.length) {
        this.chunks.shift();
      } else {
        this.chunks[0] = chunk.subarray(take);
      }
    }
    return output;
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

function parseEndpoint(endpoint: string): URL {
  try {
    return new URL(endpoint);
  } catch (cause) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint is invalid.', cause);
  }
}

async function connectTcp(endpoint: URL, connectTimeoutMs: number, signal?: AbortSignal): Promise<net.Socket> {
  return await connectSocket(endpoint, connectTimeoutMs, signal, (port, host) => net.connect({ port, host, keepAlive: true }));
}

async function connectTls(
  endpoint: URL,
  options: RequiredZlinkStreamConnectorOptions,
  signal?: AbortSignal
): Promise<tls.TLSSocket> {
  return await connectSocket(endpoint, options.connectTimeoutMs, signal, (port, host) => tls.connect({
    port,
    host,
    servername: host,
    rejectUnauthorized: !options.skipServerCertificateValidation
  }));
}

async function connectSocket<TSocket extends net.Socket>(
  endpoint: URL,
  connectTimeoutMs: number,
  signal: AbortSignal | undefined,
  create: (port: number, host: string) => TSocket
): Promise<TSocket> {
  const port = Number(endpoint.port);
  if (!Number.isInteger(port) || port <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ConfigurationError, 'Endpoint port is required.');
  }
  const socket = create(port, endpoint.hostname);
  return await new Promise<TSocket>((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect timed out.'));
    }, connectTimeoutMs);
    const cleanup = () => {
      clearTimeout(timeout);
      socket.off('connect', onConnect);
      socket.off('secureConnect', onConnect);
      socket.off('error', onError);
      signal?.removeEventListener('abort', onAbort);
    };
    const onConnect = () => {
      cleanup();
      resolve(socket);
    };
    const onError = (error: Error) => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.ConnectTimeout, 'Connect failed.', error));
    };
    const onAbort = () => {
      cleanup();
      socket.destroy();
      reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Connect canceled.'));
    };
    socket.once('connect', onConnect);
    socket.once('secureConnect', onConnect);
    socket.once('error', onError);
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

function validatePositive(value: number, name: string): void {
  if (value <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, `${name} must be positive.`);
  }
}

function validateHeartbeat(options: ZlinkStreamHeartbeatOptions | undefined): void {
  const enabled = options?.enabled ?? true;
  const intervalMs = options?.intervalMs ?? 1000;
  const timeoutMs = options?.timeoutMs ?? 5000;
  if (!enabled) {
    return;
  }
  validatePositive(intervalMs, 'Heartbeat interval');
  validatePositive(timeoutMs, 'Heartbeat timeout');
  if (timeoutMs <= intervalMs) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Heartbeat timeout must be greater than the heartbeat interval.');
  }
}

function validateReconnect(options: ZlinkStreamReconnectOptions | undefined): void {
  const enabled = options?.enabled ?? true;
  if (!enabled) {
    return;
  }
  validatePositive(options?.initialDelayMs ?? 250, 'Reconnect InitialDelay');
  validatePositive(options?.maxDelayMs ?? 5000, 'Reconnect MaxDelay');
  if ((options?.backoffFactor ?? 2.0) < 1.0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Reconnect BackoffFactor must be at least 1.0.');
  }
  if ((options?.maxAttempts ?? 3) <= 0) {
    throw connectorError(ZlinkStreamErrorCode.ValidationFailed, 'Reconnect MaxAttempts must be null or positive.');
  }
}

function connectorError(code: ZlinkStreamErrorCode, message: string, cause?: unknown): ZlinkStreamException {
  return new ZlinkStreamException({ code, message, cause });
}

function toStreamError(cause: unknown, code: ZlinkStreamErrorCode, message: string): ZlinkStreamError {
  if (cause instanceof ZlinkStreamException) {
    return cause.error;
  }
  return { code, message, cause };
}

function unwrapStreamError(error: unknown): ZlinkStreamError {
  if (error instanceof ZlinkStreamException) {
    return error.error;
  }
  return { code: ZlinkStreamErrorCode.RemoteError, message: error instanceof Error ? error.message : String(error), cause: error };
}

function subscription(dispose: () => void): Disposable {
  return { dispose };
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) {
    throw connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.');
  }
}

function delay(delayMs: number, signal: AbortSignal | undefined): Promise<void> {
  throwIfAborted(signal);
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      signal?.removeEventListener('abort', onAbort);
      resolve();
    }, delayMs);
    const onAbort = () => {
      clearTimeout(timeout);
      reject(connectorError(ZlinkStreamErrorCode.Disconnected, 'Operation canceled.'));
    };
    signal?.addEventListener('abort', onAbort, { once: true });
  });
}

function readLength(source: Uint8Array, offset: number, bytes: 1 | 2, label: string): number {
  if (source.length - offset < bytes) {
    throw connectorError(ZlinkStreamErrorCode.FrameDecodeFailed, `Metadata ${label} length is missing.`);
  }
  return bytes === 1 ? source[offset] : readUInt16BE(source, offset);
}

function writeUInt16BE(destination: Uint8Array, offset: number, value: number): void {
  destination[offset] = (value >>> 8) & 0xff;
  destination[offset + 1] = value & 0xff;
}

function writeUInt32BE(destination: Uint8Array, offset: number, value: number): void {
  destination[offset] = (value >>> 24) & 0xff;
  destination[offset + 1] = (value >>> 16) & 0xff;
  destination[offset + 2] = (value >>> 8) & 0xff;
  destination[offset + 3] = value & 0xff;
}

function readUInt16BE(source: Uint8Array, offset: number): number {
  return (source[offset] << 8) | source[offset + 1];
}

function readUInt32BE(source: Uint8Array, offset: number): number {
  return (
    source[offset] * 0x1000000
    + ((source[offset + 1] << 16) | (source[offset + 2] << 8) | source[offset + 3])
  );
}

function writeBigUInt64BE(destination: Uint8Array, offset: number, value: bigint): void {
  for (let i = 7; i >= 0; i--) {
    destination[offset + i] = Number(value & 0xffn);
    value >>= 8n;
  }
}

function readBigUInt64BE(source: Uint8Array, offset: number): bigint {
  let value = 0n;
  for (let i = 0; i < 8; i++) {
    value = (value << 8n) | BigInt(source[offset + i]);
  }
  return value;
}

function utf8Encode(value: string): Uint8Array {
  return new TextEncoder().encode(value);
}

function utf8Decode(value: Uint8Array): string {
  return new TextDecoder().decode(value);
}
