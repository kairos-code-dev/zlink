import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  Disposable,
  ZlinkStreamConnector,
  ZlinkStreamEncodedPayload,
  ZlinkStreamError,
  ZlinkStreamMessage,
  ZlinkStreamMetadata,
  ZlinkStreamRequestCall,
  ZlinkStreamResult,
  ZlinkStreamResultOf,
  ZlinkStreamSendCall
} from '@zlink-systems/stream-connector';

interface StreamConnectorRuntime {
  readonly ZlinkStreamCodec: {
    readonly Json: number;
  };
}

const streamConnector = loadStreamConnector();

export const zlinkStreamJsonCodecName = 'json';

export interface ZlinkStreamJsonCodecOptions {
  readonly replacer?: (this: unknown, key: string, value: unknown) => unknown;
  readonly reviver?: (this: unknown, key: string, value: unknown) => unknown;
}

let codecOptions: ZlinkStreamJsonCodecOptions = {};

export const zlinkStreamJsonCodec = {
  configure(options: ZlinkStreamJsonCodecOptions): void {
    codecOptions = options;
  }
};

export function toJson<T>(value: T, messageType?: Function): ZlinkStreamEncodedPayload {
  return {
    codec: streamConnector.ZlinkStreamCodec.Json,
    payload: new TextEncoder().encode(JSON.stringify(value, codecOptions.replacer)),
    messageType: messageType ?? inferMessageType(value)
  };
}

export function fromJson<T>(payload: ZlinkStreamEncodedPayload): T {
  ensureJson(payload);
  return JSON.parse(new TextDecoder().decode(payload.payload), codecOptions.reviver) as T;
}

export function sendJson<T>(connector: ZlinkStreamConnector, payload: T, messageType?: Function): ZlinkStreamJsonSendBuilder {
  return new ZlinkStreamJsonSendBuilder(connector.send(toJson(payload, messageType)));
}

export function requestJson<T>(connector: ZlinkStreamConnector, payload: T, messageType?: Function): ZlinkStreamJsonRequestBuilder {
  return new ZlinkStreamJsonRequestBuilder(connector.request(toJson(payload, messageType)));
}

export function onJson<T>(
  connector: ZlinkStreamConnector,
  nameOrHandler: string | ((message: ZlinkStreamMessage<T>, signal?: AbortSignal) => Promise<void> | void),
  handler?: (message: ZlinkStreamMessage<T>, signal?: AbortSignal) => Promise<void> | void
): Disposable {
  const name = typeof nameOrHandler === 'string'
    ? nameOrHandler
    : undefined;
  const selectedHandler = typeof nameOrHandler === 'function'
    ? nameOrHandler
    : handler;
  if (selectedHandler === undefined) {
    throw new Error('JSON stream handler is required.');
  }
  const packetName = name ?? selectedHandler.name;
  if (packetName.length === 0) {
    throw new Error('Packet name is required for JSON stream handler.');
  }
  return connector.on(packetName, (message, signal) => selectedHandler({
    name: message.name,
    metadata: message.metadata,
    payload: fromJson<T>(message.payload)
  }, signal));
}

export class ZlinkStreamJsonSendBuilder {
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

export class ZlinkStreamJsonRequestBuilder {
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
    return fromJson<TReply>(reply);
  }

  submitCallback(callback: (result: ZlinkStreamResult) => void): void {
    this.inner.submit(callback);
  }

  submitJsonCallback<TReply>(callback: (result: ZlinkStreamResultOf<TReply>) => void): void {
    this.inner.submit((result) => {
      if (!result.isSuccess || result.value === undefined) {
        callback({ isSuccess: false, error: result.error });
        return;
      }
      try {
        callback({ isSuccess: true, value: fromJson<TReply>(result.value) });
      } catch (cause) {
        callback({
          isSuccess: false,
          error: {
            code: 'userCallbackFailed' as ZlinkStreamError['code'],
            message: 'JSON reply decode failed.',
            cause
          }
        });
      }
    });
  }
}

function ensureJson(payload: ZlinkStreamEncodedPayload): void {
  if (payload.codec !== streamConnector.ZlinkStreamCodec.Json) {
    throw new Error(`Stream payload codec is ${payload.codec}, not Json.`);
  }
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

function inferMessageType(value: unknown): Function | undefined {
  if (value === null || value === undefined) {
    return undefined;
  }
  const constructor = Object.getPrototypeOf(value)?.constructor;
  return constructor === Object ? undefined : constructor;
}
