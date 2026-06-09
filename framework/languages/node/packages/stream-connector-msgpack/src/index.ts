import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  ZlinkStreamEncodedPayload,
  ZlinkStreamPayloadCodec
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

export const zlinkStreamMessagePackCodec: ZlinkStreamPayloadCodec = {
  encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
    return toMsgPack(payload, messageType);
  },

  decode<T = unknown>(payload: ZlinkStreamEncodedPayload): T {
    return fromMsgPack<T>(payload);
  }
};

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
