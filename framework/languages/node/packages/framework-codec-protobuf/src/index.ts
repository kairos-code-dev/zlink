import {
  ZLinkEncodedPayload,
  type ZLinkCodecExtension,
  type ZLinkCodecRegistrar,
  type ZLinkMessageSerializer
} from '@zlink-systems/framework';
import {
  ZlinkStreamCodec,
  type ZlinkStreamEncodedPayload,
  type ZlinkStreamPayloadCodec
} from '@zlink-systems/stream-connector';
import {
  createDynamicValueProtobufType,
  decodeDynamicValue,
  encodeDynamicValue
} from './dynamic-value-wire';
import { createProtobufJsEnvelopeOptions } from './protobufjs-envelope';

export const ZLINK_PROTOBUF_CONTENT_TYPE = 'application/x-protobuf';
export const zlinkStreamProtobufCodecName = 'protobuf';

export interface ProtobufType<T> {
  encode(message: T): { finish(): Uint8Array };
  decode(reader: Uint8Array): T;
}

export interface ProtobufEnvelopeCodecOptions {
  encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload;
  decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload;
}

export interface ProtobufJsEnvelopeCodecOptions {
  protoPath: string;
  packageName: string;
  envelopeType: string;
  messageTypesByPacketName?: Readonly<Record<string, string>>;
  responseTypesByRequestPacketName?: Readonly<Record<string, string>>;
  inferredTypesByConstructorName?: ReadonlySet<string>;
  inferMessageName?: (
    value: unknown,
    context: {
      messageType?: Function;
      packetName?: string;
      mappedType?: string;
      responseType?: string;
      hasType(typeName: string): boolean;
    }
  ) => string | undefined;
}

export type ZLinkProtobufCodecExtension = ZLinkCodecExtension & ZlinkStreamPayloadCodec;

export type ZLinkProtobufEnvelopeCodecExtension = ZLinkProtobufCodecExtension & {
  encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload;
};

export function zlinkProtobufCodec(): ZLinkProtobufCodecExtension {
  return {
    register(codecs: ZLinkCodecRegistrar): void {
      codecs.addSerializer(ZLINK_PROTOBUF_CONTENT_TYPE, createProtobufMessageSerializer());
      codecs.addStreamCodec(ZLINK_PROTOBUF_CONTENT_TYPE, this);
    },

    encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
      return toProto(payload, createDynamicValueProtobufType(), messageType);
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      return fromProto(payload, createDynamicValueProtobufType()) as unknown as TPayload;
    }
  };
}

export function createProtobufMessageSerializer(): ZLinkMessageSerializer {
  return {
    serialize<T>(value: T): ZLinkEncodedPayload {
      return ZLinkEncodedPayload.from(encodeDynamicValue(value));
    },

    deserialize<T>(payload: ZLinkEncodedPayload): T {
      return decodeDynamicValue(Buffer.from(payload.data())) as T;
    }
  };
}

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

export function createZlinkStreamProtobufEnvelopeCodec(
  options: ProtobufEnvelopeCodecOptions
): ZLinkProtobufEnvelopeCodecExtension {
  return {
    register(codecs: ZLinkCodecRegistrar): void {
      codecs.addSerializer(ZLINK_PROTOBUF_CONTENT_TYPE, createProtobufMessageSerializer());
      codecs.addStreamCodec(ZLINK_PROTOBUF_CONTENT_TYPE, this);
    },

    encode(payload: unknown, messageType?: Function, packetName?: string): ZlinkStreamEncodedPayload {
      return options.encode(payload, messageType, packetName);
    },

    decode<TPayload = unknown>(payload: ZlinkStreamEncodedPayload): TPayload {
      return options.decode<TPayload>(payload);
    }
  };
}

export function createZlinkProtobufJsEnvelopeCodec(
  options: ProtobufJsEnvelopeCodecOptions
): ZLinkProtobufEnvelopeCodecExtension {
  return createZlinkStreamProtobufEnvelopeCodec(createProtobufJsEnvelopeOptions(options));
}

export function toProto<T>(value: T, type: ProtobufType<T>, messageType?: Function): ZlinkStreamEncodedPayload {
  return {
    codec: ZlinkStreamCodec.Protobuf,
    payload: type.encode(value).finish(),
    messageType: messageType ?? inferMessageType(value)
  };
}

export function fromProto<T>(payload: ZlinkStreamEncodedPayload, type: ProtobufType<T>): T {
  ensureProtobuf(payload);
  return type.decode(payload.payload);
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
