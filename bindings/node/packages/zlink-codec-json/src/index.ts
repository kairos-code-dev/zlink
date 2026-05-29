import type { Message } from "@zlink-systems/zlink";

function loadMessageClass(): { from(data: Buffer | Uint8Array): Message } {
  try {
    return require("@zlink-systems/zlink").Message;
  } catch {
    return require("../../../dist/index.js").Message;
  }
}

export function encode<T>(value: T): Message {
  const bytes = Buffer.from(JSON.stringify(value), "utf8");
  return loadMessageClass().from(bytes);
}

export function decode<T>(msg: Message): T {
  return JSON.parse(msg.data().toString("utf8")) as T;
}
