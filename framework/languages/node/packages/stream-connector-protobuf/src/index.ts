import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  ZlinkStreamEncodedPayload,
  ZlinkStreamPayloadCodec
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

export function createZlinkStreamProtobufCodec<T>(type: ProtobufType<T>): ZlinkStreamPayloadCodec {
  return {
    encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
      return toProto(payload as T, type, messageType);
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      return fromProto(payload, type) as unknown as TPayload;
    }
  };
}

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
