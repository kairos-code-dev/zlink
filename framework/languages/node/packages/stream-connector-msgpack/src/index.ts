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

export const zlinkStreamMessagePackCodecName = 'messagepack';

interface StreamConnectorRuntime {
  readonly ZlinkStreamCodec: {
    readonly MessagePack: number;
  };
}

interface MessagePackRuntime {
  encode(value: unknown): Uint8Array;
  decode(value: Uint8Array): unknown;
}

const streamConnector = loadStreamConnector();
const msgpack = loadMessagePack();

export function toMsgPack<T>(value: T, messageType?: Function): ZlinkStreamEncodedPayload {
  return {
    codec: streamConnector.ZlinkStreamCodec.MessagePack,
    payload: msgpack.encode(value),
    messageType: messageType ?? inferMessageType(value)
  };
}

export function fromMsgPack<T>(payload: ZlinkStreamEncodedPayload): T {
  ensureMessagePack(payload);
  return msgpack.decode(payload.payload) as T;
}

export function sendMsgPack<T>(connector: ZlinkStreamConnector, payload: T, messageType?: Function): ZlinkStreamMessagePackSendBuilder {
  return new ZlinkStreamMessagePackSendBuilder(connector.send(toMsgPack(payload, messageType)));
}

export function requestMsgPack<T>(connector: ZlinkStreamConnector, payload: T, messageType?: Function): ZlinkStreamMessagePackRequestBuilder {
  return new ZlinkStreamMessagePackRequestBuilder(connector.request(toMsgPack(payload, messageType)));
}

export function onMsgPack<T>(
  connector: ZlinkStreamConnector,
  name: string,
  handler: (message: ZlinkStreamMessage<T>, signal?: AbortSignal) => Promise<void> | void
) {
  return connector.on(name, (message, signal) => handler({
    name: message.name,
    metadata: message.metadata,
    payload: fromMsgPack<T>(message.payload)
  }, signal));
}

export class ZlinkStreamMessagePackSendBuilder {
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

export class ZlinkStreamMessagePackRequestBuilder {
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

  async submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    const reply = await this.inner.submit(signal);
    return fromMsgPack<TReply>(reply);
  }

  submitCallback(callback: (result: ZlinkStreamResult) => void): void {
    this.inner.submit(callback);
  }

  submitMsgPackCallback<TReply>(callback: (result: ZlinkStreamResultOf<TReply>) => void): void {
    this.inner.submit((result) => {
      if (!result.isSuccess || result.value === undefined) {
        callback({ isSuccess: false, error: result.error });
        return;
      }
      try {
        callback({ isSuccess: true, value: fromMsgPack<TReply>(result.value) });
      } catch (cause) {
        callback({
          isSuccess: false,
          error: {
            code: 'userCallbackFailed' as NonNullable<ZlinkStreamResult['error']>['code'],
            message: 'MessagePack reply decode failed.',
            cause
          }
        });
      }
    });
  }
}

function ensureMessagePack(payload: ZlinkStreamEncodedPayload): void {
  if (payload.codec !== streamConnector.ZlinkStreamCodec.MessagePack) {
    throw new Error(`Stream payload codec is ${payload.codec}, not MessagePack.`);
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

function loadMessagePack(): MessagePackRuntime {
  const requireMsgPack = createRequire(__filename);
  try {
    return requireMsgPack('@msgpack/msgpack') as MessagePackRuntime;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    return requireMsgPack(path.resolve(__dirname, '../../../../../../bindings/node/node_modules/@msgpack/msgpack')) as MessagePackRuntime;
  }
}
