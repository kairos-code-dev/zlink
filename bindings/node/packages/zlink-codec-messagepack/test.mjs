import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const codec = require("./dist/index.js");

try {
  let msgpack;
  try {
    msgpack = require("@msgpack/msgpack");
  } catch (error) {
    console.log(`[messagepack] SKIP @msgpack/msgpack is not installed: ${error.message}`);
    process.exit(0);
  }

  const sample = {
    kind: "messagepack",
    seq: 11,
    nested: { enabled: true, values: [1, 4, 9] },
  };
  const bytes = msgpack.encode(sample);

  const message = codec.encode(sample);
  assert.equal(message.constructor.name, "Message");
  assert.deepEqual(codec.decode(message), sample);
  assert.deepEqual(msgpack.decode(bytes), sample);
  console.log("[messagepack] PASS codec Message roundtrip");
} catch (error) {
  console.error("[messagepack] FAIL", error);
  process.exitCode = 1;
}
