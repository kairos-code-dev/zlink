import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  ZlinkStreamConnector,
  ZlinkStreamEncodedPayload,
  ZlinkStreamMessage,
  ZlinkStreamMetadata,
  ZlinkStreamRequestCall,
  ZlinkStreamResult,
  ZlinkStreamResultOf,
  ZlinkStreamSendCall
} from '@zlink-systems/stream-connector';

export const zlinkStreamProtobufCodecName = 'protobuf';

interface StreamConnectorRuntime {
  readonly ZlinkStreamCodec: {
    readonly Protobuf: number;
  };
}

export interface ProtobufType<T> {
  encode(message: T): { finish(): Uint8Array };
  decode(reader: Uint8Array): T;
}

const streamConnector = loadStreamConnector();

export function toProto<T>(value: T, type: ProtobufType<T>, messageType?: Function): ZlinkStreamEncodedPayload {
  return {
    codec: streamConnector.ZlinkStreamCodec.Protobuf,
    payload: type.encode(value).finish(),
    messageType: messageType ?? inferMessageType(value)
  };
}

export function fromProto<T>(payload: ZlinkStreamEncodedPayload, type: ProtobufType<T>): T {
  ensureProtobuf(payload);
  return type.decode(payload.payload);
}

export function sendProto<T>(
  connector: ZlinkStreamConnector,
  payload: T,
  type: ProtobufType<T>,
  messageType?: Function
): ZlinkStreamProtobufSendBuilder {
  return new ZlinkStreamProtobufSendBuilder(connector.send(toProto(payload, type, messageType)));
}

export function requestProto<T>(
  connector: ZlinkStreamConnector,
  payload: T,
  type: ProtobufType<T>,
  messageType?: Function
): ZlinkStreamProtobufRequestBuilder {
  return new ZlinkStreamProtobufRequestBuilder(connector.request(toProto(payload, type, messageType)));
}

export function onProto<T>(
  connector: ZlinkStreamConnector,
  name: string,
  type: ProtobufType<T>,
  handler: (message: ZlinkStreamMessage<T>, signal?: AbortSignal) => Promise<void> | void
) {
  return connector.on(name, (message, signal) => handler({
    name: message.name,
    metadata: message.metadata,
    payload: fromProto<T>(message.payload, type)
  }, signal));
}

export class ZlinkStreamProtobufSendBuilder {
  constructor(private readonly inner: ZlinkStreamSendCall) {}

  packetName(name: string): this {
    this.inner.packetName(name);
    return this;
  }

  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  metadata(keyOrMetadata: string | ZlinkStreamMetadata, value?: string): this {
    if (typeof keyOrMetadata === 'string') {
      this.inner.metadata(keyOrMetadata, value ?? '');
    } else {
      this.inner.metadata(keyOrMetadata);
    }
    return this;
  }

  compress(): this {
    this.inner.compress();
    return this;
  }

  submit(signal?: AbortSignal): Promise<void> {
    return this.inner.submit(signal);
  }
}

export class ZlinkStreamProtobufRequestBuilder {
  constructor(private readonly inner: ZlinkStreamRequestCall) {}

  packetName(name: string): this {
    this.inner.packetName(name);
    return this;
  }

  metadata(key: string, value: string): this;
  metadata(metadata: ZlinkStreamMetadata): this;
  metadata(keyOrMetadata: string | ZlinkStreamMetadata, value?: string): this {
    if (typeof keyOrMetadata === 'string') {
      this.inner.metadata(keyOrMetadata, value ?? '');
    } else {
      this.inner.metadata(keyOrMetadata);
    }
    return this;
  }

  timeout(timeoutMs: number): this {
    this.inner.timeout(timeoutMs);
    return this;
  }

  compress(): this {
    this.inner.compress();
    return this;
  }

  async submit<TReply>(type: ProtobufType<TReply>, signal?: AbortSignal): Promise<TReply> {
    const reply = await this.inner.submit(signal);
    return fromProto<TReply>(reply, type);
  }

  submitCallback(callback: (result: ZlinkStreamResult) => void): void {
    this.inner.submit(callback);
  }

  submitProtoCallback<TReply>(type: ProtobufType<TReply>, callback: (result: ZlinkStreamResultOf<TReply>) => void): void {
    this.inner.submit((result) => {
      if (!result.isSuccess || result.value === undefined) {
        callback({ isSuccess: false, error: result.error });
        return;
      }
      try {
        callback({ isSuccess: true, value: fromProto<TReply>(result.value, type) });
      } catch (cause) {
        callback({
          isSuccess: false,
          error: {
            code: 'userCallbackFailed' as NonNullable<ZlinkStreamResult['error']>['code'],
            message: 'Protobuf reply decode failed.',
            cause
          }
        });
      }
    });
  }
}

export function loadProtobufJs(): unknown {
  const requireProtobuf = createRequire(__filename);
  try {
    return requireProtobuf('protobufjs') as unknown;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    return requireProtobuf(path.resolve(__dirname, '../../../../../../bindings/node/node_modules/protobufjs')) as unknown;
  }
}

function ensureProtobuf(payload: ZlinkStreamEncodedPayload): void {
  if (payload.codec !== streamConnector.ZlinkStreamCodec.Protobuf) {
    throw new Error(`Stream payload codec is ${payload.codec}, not Protobuf.`);
  }
}

function inferMessageType(value: unknown): Function | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  const constructor = Object.getPrototypeOf(value)?.constructor;
  return constructor === Object ? undefined : constructor;
}

function loadStreamConnector(): StreamConnectorRuntime {
  const requireConnector = createRequire(__filename);
  try {
    return requireConnector('@zlink-systems/stream-connector') as StreamConnectorRuntime;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    return requireConnector(path.resolve(__dirname, '../../stream-connector/dist')) as StreamConnectorRuntime;
  }
}
