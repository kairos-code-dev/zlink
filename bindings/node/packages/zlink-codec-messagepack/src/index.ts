import type { Message } from "@zlink-systems/zlink";
import * as msgpack from "@msgpack/msgpack";

export function encode<T>(value: T): Message {
  const { Message: ZMsg } = require("@zlink-systems/zlink");
  const bytes = msgpack.encode(value);
  return ZMsg.from(bytes);
}

export function decode<T>(msg: Message): T {
  return msgpack.decode(msg.data()) as T;
}
