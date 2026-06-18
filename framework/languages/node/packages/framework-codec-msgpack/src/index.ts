import { Message } from '@zlink-systems/zlink';
import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  ZLinkCodecExtension,
  ZLinkCodecRegistryBuilder,
  ZLinkMessageSerializer
} from '@zlink-systems/framework';
import {
  ZlinkStreamCodec,
  type ZlinkStreamEncodedPayload,
  type ZlinkStreamPayloadCodec
} from '@zlink-systems/stream-connector';

export const ZLINK_MESSAGEPACK_CONTENT_TYPE = 'application/x-msgpack';
export const zlinkStreamMessagePackCodecName = 'messagepack';

interface MessagePackRuntime {
  encode(value: unknown): Uint8Array;
  decode(value: Uint8Array): unknown;
}

const msgpack = loadMessagePack();

export type ZLinkMessagePackCodecExtension = ZLinkCodecExtension & ZlinkStreamPayloadCodec;

export function zlinkMessagePackCodec(): ZLinkMessagePackCodecExtension {
  return {
    register(codecs: ZLinkCodecRegistryBuilder): void {
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
    serialize<T>(value: T): Message {
      return Message.from(encodeMessagePack(value));
    },

    deserialize<T>(message: { data(): Uint8Array }): T {
      return decodeMessagePack(message.data()) as T;
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

function loadMessagePack(): MessagePackRuntime {
  const requireMsgPack = createRequire(__filename);
  try {
    return requireMsgPack('@msgpack/msgpack') as MessagePackRuntime;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    const zlinkEntryPath = requireMsgPack.resolve('@zlink-systems/zlink');
    return requireMsgPack(path.resolve(path.dirname(zlinkEntryPath), '..', 'node_modules', '@msgpack', 'msgpack')) as MessagePackRuntime;
  }
}
