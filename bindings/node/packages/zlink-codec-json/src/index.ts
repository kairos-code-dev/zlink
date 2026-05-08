import type { Message } from "@zlink-systems/zlink";

export function encode<T>(value: T): Message {
  const { Message: ZMsg } = require("@zlink-systems/zlink");
  const bytes = Buffer.from(JSON.stringify(value), "utf8");
  return ZMsg.from(bytes);
}

export function decode<T>(msg: Message): T {
  return JSON.parse(msg.data().toString("utf8")) as T;
}
