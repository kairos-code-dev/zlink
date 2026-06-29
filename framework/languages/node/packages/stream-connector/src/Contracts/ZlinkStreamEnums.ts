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
  PayloadCompressed = 0x04,
  HasCorrelationId = 0x08
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
  ObserverFailed = 'observer-failed',
  ObserverDropped = 'observer-dropped',
  ReceivedMessageDropped = 'received-message-dropped',
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
