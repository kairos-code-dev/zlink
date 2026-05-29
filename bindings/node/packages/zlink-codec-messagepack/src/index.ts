import type { Message } from "@zlink-systems/zlink";
import * as msgpack from "@msgpack/msgpack";

function loadMessageClass(): { from(data: Buffer | Uint8Array): Message } {
  try {
    return require("@zlink-systems/zlink").Message;
  } catch {
    return require("../../../dist/index.js").Message;
  }
}

export function encode<T>(value: T): Message {
  const bytes = msgpack.encode(value);
  return loadMessageClass().from(bytes);
}

export function decode<T>(msg: Message): T {
  return msgpack.decode(msg.data()) as T;
}
