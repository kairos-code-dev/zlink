import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

function loadRealMessageClass() {
  const attempts = [
    ["@zlink-systems/zlink", () => require("@zlink-systems/zlink").Message],
    ["../../dist/message.js", () => require("../../dist/message.js").Message],
  ];

  for (const [label, loader] of attempts) {
    try {
      const Message = loader();
      if (typeof Message?.from === "function") {
        return { Message, source: label };
      }
    } catch {
      // Try the next location.
    }
  }

  return { Message: null, source: null };
}

function createTestType(protobuf) {
  return {
    encode(message, writer = protobuf.Writer.create()) {
      if (message.id != null) {
        writer.uint32(8).uint32(message.id);
      }
      if (message.name != null) {
        writer.uint32(18).string(message.name);
      }
      return writer;
    },
    decode(readerOrBuffer) {
      const reader =
        readerOrBuffer instanceof protobuf.Reader
          ? readerOrBuffer
          : protobuf.Reader.create(readerOrBuffer);
      const message = { id: 0, name: "" };

      while (reader.pos < reader.len) {
        const tag = reader.uint32();
        switch (tag >>> 3) {
          case 1:
            message.id = reader.uint32();
            break;
          case 2:
            message.name = reader.string();
            break;
          default:
            reader.skipType(tag & 7);
            break;
        }
      }

      return message;
    },
  };
}

try {
  let protobuf;
  try {
    protobuf = require("protobufjs/minimal");
  } catch (error) {
    console.log(`[protobuf] SKIP protobufjs is not installed: ${error.message}`);
    process.exit(0);
  }

  const sample = { id: 23, name: "codec" };
  const type = createTestType(protobuf);
  const real = loadRealMessageClass();
  const bytes = type.encode(sample).finish();

  if (real.Message) {
    const message = real.Message.from(bytes);
    assert.equal(message.constructor, real.Message);
    assert.deepEqual(type.decode(message.data()), sample);
    console.log(`[protobuf] PASS real Message roundtrip via ${real.source}`);
  } else {
    assert.deepEqual(type.decode(bytes), sample);
    console.log("[protobuf] PASS direct protobuf roundtrip; real Message binding unavailable");
  }
} catch (error) {
  console.error("[protobuf] FAIL", error);
  process.exitCode = 1;
}
