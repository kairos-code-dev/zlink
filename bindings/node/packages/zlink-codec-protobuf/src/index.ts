import type { Message } from "@zlink-systems/zlink";
import type { Writer, Reader } from "protobufjs";

export interface ProtobufType<T> {
  encode(message: T, writer?: Writer): Writer;
  decode(reader: Reader | Uint8Array): T;
}

export function encode<T>(value: T, type: ProtobufType<T>): Message {
  const { Message: ZMsg } = require("@zlink-systems/zlink");
  const bytes = type.encode(value).finish();
  return ZMsg.from(bytes);
}

export function decode<T>(msg: Message, type: ProtobufType<T>): T {
  return type.decode(msg.data());
}
