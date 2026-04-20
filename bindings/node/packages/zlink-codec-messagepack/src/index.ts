import type { Message } from "@ulalax/zlink";
import * as msgpack from "@msgpack/msgpack";

export function encode<T>(value: T): Message {
  const { Message: ZMsg } = require("@ulalax/zlink");
  const bytes = msgpack.encode(value);
  return ZMsg.from(bytes);
}

export function decode<T>(msg: Message): T {
  return msgpack.decode(msg.data()) as T;
}
