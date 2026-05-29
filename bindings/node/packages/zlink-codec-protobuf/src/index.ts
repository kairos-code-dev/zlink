import type { Message } from "@zlink-systems/zlink";
import type { Writer, Reader } from "protobufjs";

function loadMessageClass(): { from(data: Buffer | Uint8Array): Message } {
  try {
    return require("@zlink-systems/zlink").Message;
  } catch {
    return require("../../../dist/index.js").Message;
  }
}

export interface ProtobufType<T> {
  encode(message: T, writer?: Writer): Writer;
  decode(reader: Reader | Uint8Array): T;
}

export function encode<T>(value: T, type: ProtobufType<T>): Message {
  const bytes = type.encode(value).finish();
  return loadMessageClass().from(bytes);
}

export function decode<T>(msg: Message, type: ProtobufType<T>): T {
  return type.decode(msg.data());
}
