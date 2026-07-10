import * as msgpack from '@msgpack/msgpack';
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

export const ZLINK_MESSAGEPACK_CONTENT_TYPE = 'application/x-msgpack';
export const zlinkStreamMessagePackCodecName = 'messagepack';

export type ZLinkMessagePackCodecExtension = ZLinkCodecExtension & ZlinkStreamPayloadCodec;

export function zlinkMessagePackCodec(): ZLinkMessagePackCodecExtension {
  return {
    register(codecs: ZLinkCodecRegistrar): void {
      codecs.addSerializer(ZLINK_MESSAGEPACK_CONTENT_TYPE, createMessagePackSerializer());
      codecs.addStreamCodec(ZLINK_MESSAGEPACK_CONTENT_TYPE, this);
    },

    encode(payload: unknown, messageType?: Function): ZlinkStreamEncodedPayload {
      return toMsgPack(payload, messageType);
    },

    decode<T = unknown>(payload: ZlinkStreamEncodedPayload): T {
      return fromMsgPack<T>(payload);
    }
  };
}

export const zlinkStreamMessagePackCodec: ZlinkStreamPayloadCodec = zlinkMessagePackCodec();

export function toMsgPack<T>(value: T, messageType?: Function): ZlinkStreamEncodedPayload {
  return {
    codec: ZlinkStreamCodec.MessagePack,
    payload: encodeMessagePack(value),
    messageType: messageType ?? inferMessageType(value)
  };
}

export function fromMsgPack<T>(payload: ZlinkStreamEncodedPayload): T {
  ensureMessagePack(payload);
  return decodeMessagePack(payload.payload) as T;
}

export function createMessagePackSerializer(): ZLinkMessageSerializer {
  return {
    serialize<T>(value: T): ZLinkEncodedPayload {
      return ZLinkEncodedPayload.from(encodeMessagePack(value));
    },

    deserialize<T>(payload: ZLinkEncodedPayload): T {
      return decodeMessagePack(payload.data()) as T;
    }
  };
}

function encodeMessagePack(value: unknown): Uint8Array {
  return msgpack.encode(value);
}

function decodeMessagePack(value: Uint8Array): unknown {
  return msgpack.decode(value);
}

function ensureMessagePack(payload: ZlinkStreamEncodedPayload): void {
  if (payload.codec !== ZlinkStreamCodec.MessagePack) {
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
