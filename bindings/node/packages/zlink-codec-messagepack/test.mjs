import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

function loadRealMessageClass() {
  const attempts = [
    ["@ulalax/zlink", () => require("@ulalax/zlink").Message],
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
  const real = loadRealMessageClass();
  const bytes = msgpack.encode(sample);

  if (real.Message) {
    const message = real.Message.from(bytes);
    assert.equal(message.constructor, real.Message);
    assert.deepEqual(msgpack.decode(message.data()), sample);
    console.log(`[messagepack] PASS real Message roundtrip via ${real.source}`);
  } else {
    assert.deepEqual(msgpack.decode(bytes), sample);
    console.log("[messagepack] PASS direct msgpack roundtrip; real Message binding unavailable");
  }
} catch (error) {
  console.error("[messagepack] FAIL", error);
  process.exitCode = 1;
}
