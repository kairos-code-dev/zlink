import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const codec = require("./dist/index.js");

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
  const bytes = type.encode(sample).finish();

  const message = codec.encode(sample, type);
  assert.equal(message.constructor.name, "Message");
  assert.deepEqual(codec.decode(message, type), sample);
  assert.deepEqual(type.decode(bytes), sample);
  console.log("[protobuf] PASS codec Message roundtrip");
} catch (error) {
  console.error("[protobuf] FAIL", error);
  process.exitCode = 1;
}
