import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  ZlinkStreamEncodedPayload,
  ZlinkStreamPayloadCodec
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

export const zlinkStreamJsonCodec: ZlinkStreamPayloadCodec & {
  configure(options: ZlinkStreamJsonCodecOptions): void;
} = {
  configure(options: ZlinkStreamJsonCodecOptions): void {
    codecOptions = options;
  },

  encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
    return toJson(payload, messageType);
  },

  decode<T = unknown>(payload: ZlinkStreamEncodedPayload): T {
    return fromJson<T>(payload);
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
  return JSON.parse(new TextDecoder().decode(payload.payload), safeJsonReviver) as T;
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

function safeJsonReviver(this: unknown, key: string, value: unknown): unknown {
  if (isPrototypeKey(key)) {
    throw new Error(`JSON payload key '${key}' is not allowed.`);
  }
  if (codecOptions.reviver !== undefined) {
    return codecOptions.reviver.call(this, key, value);
  }
  return value;
}

function isPrototypeKey(key: string): boolean {
  return key === '__proto__' || key === 'constructor' || key === 'prototype';
}
