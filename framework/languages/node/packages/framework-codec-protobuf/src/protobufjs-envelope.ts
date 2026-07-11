import * as protobufjs from 'protobufjs';
import {
  ZlinkStreamCodec,
  type ZlinkStreamEncodedPayload
} from '@zlink-systems/stream-connector';
import type {
  ProtobufEnvelopeCodecOptions,
  ProtobufJsEnvelopeCodecOptions
} from './index';

interface ProtobufJsRoot {
  lookupType(name: string): ProtobufJsType;
}

interface ProtobufJsType {
  fromObject(value: unknown): unknown;
  toObject(message: unknown, options?: ProtobufConversionOptions): Record<string, unknown>;
  encode(message: unknown): { finish(): Uint8Array };
  decode(bytes: Uint8Array): unknown;
}

interface ProtobufConversionOptions {
  defaults?: boolean;
  arrays?: boolean;
  longs?: NumberConstructor | StringConstructor;
  bytes?: typeof Buffer | StringConstructor;
}

interface ProtobufJs {
  loadSync(filename: string): ProtobufJsRoot;
}

export function createProtobufJsEnvelopeOptions(
  options: ProtobufJsEnvelopeCodecOptions
): ProtobufEnvelopeCodecOptions {
  const protobuf = protobufjs as unknown as ProtobufJs;
  const root = protobuf.loadSync(options.protoPath);
  const lookupType = (typeName: string): ProtobufJsType =>
    root.lookupType(`${options.packageName}.${typeName}`);
  const hasType = (typeName: string): boolean => {
    try {
      lookupType(typeName);
      return true;
    } catch {
      return false;
    }
  };
  const encodeMessage = (typeName: string, value: unknown): Uint8Array => {
    const type = lookupType(typeName);
    return type.encode(type.fromObject(value)).finish();
  };
  const decodeMessage = (typeName: string, bytes: Uint8Array): Record<string, unknown> => {
    const type = lookupType(typeName);
    return type.toObject(type.decode(bytes), {
      defaults: true,
      arrays: true,
      longs: Number,
      bytes: Buffer
    });
  };
  const resolveMessageName = (
    value: unknown,
    messageType?: Function,
    packetName?: string,
    preferResponseType = false
  ): string => {
    const responseType = packetName === undefined
      ? undefined
      : options.responseTypesByRequestPacketName?.[packetName];
    if (preferResponseType && responseType !== undefined) {
      return responseType;
    }
    const mappedType = packetName === undefined
      ? undefined
      : options.messageTypesByPacketName?.[packetName];
    if (mappedType !== undefined) {
      return mappedType;
    }
    if (messageType?.name !== undefined && hasType(messageType.name)) {
      return messageType.name;
    }
    const constructor = inferMessageType(value);
    if (
      constructor?.name !== undefined
      && options.inferredTypesByConstructorName?.has(constructor.name) === true
    ) {
      return constructor.name;
    }
    const inferred = options.inferMessageName?.(value, {
      messageType,
      packetName,
      mappedType,
      responseType,
      hasType
    });
    if (inferred !== undefined) {
      return inferred;
    }
    if (value === null || value === undefined) {
      throw new Error('Cannot infer Protobuf message type for empty payload.');
    }
    throw new Error(
      `Cannot infer Protobuf message type for payload keys: ${Object.keys(value as Record<string, unknown>).join(', ')}`
    );
  };

  return {
    encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload {
      const type = resolveMessageName(payload, messageType, packetName, true);
      return {
        codec: ZlinkStreamCodec.Protobuf,
        payload: encodeMessage(options.envelopeType, { type, payload: encodeMessage(type, payload) }),
        messageType: messageType ?? inferMessageType(payload)
      };
    },
    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      ensureProtobuf(payload);
      const envelope = decodeMessage(options.envelopeType, payload.payload) as {
        type: string;
        payload: Uint8Array;
      };
      return decodeMessage(envelope.type, envelope.payload) as TPayload;
    }
  };
}

function ensureProtobuf(payload: ZlinkStreamEncodedPayload): void {
  if (payload.codec !== ZlinkStreamCodec.Protobuf) {
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
