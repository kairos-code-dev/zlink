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

export function validateMetadataKey(key: string): void {
  if (key.length === 0 || key.length > 255) {
    throw new ZlinkStreamException({ code: ZlinkStreamErrorCode.ValidationFailed, message: 'Metadata key length is invalid.' });
  }
}
