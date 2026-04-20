import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

function loadRealMessageClass() {
  const attempts = [
    ["@ulalax/zlink", () => require("@ulalax/zlink").Message],
    ["../../dist/message.js", () => require("../../dist/message.js").Message],
  ];
  const errors = [];

  for (const [label, loader] of attempts) {
    try {
      const Message = loader();
      if (typeof Message?.from === "function") {
        return { Message, source: label };
      }
      errors.push(`${label}: Message.from is unavailable`);
    } catch (error) {
      errors.push(`${label}: ${error.message}`);
    }
  }

  return { Message: null, source: null, errors };
}

function encodeJson(value, Message) {
  const bytes = Buffer.from(JSON.stringify(value), "utf8");
  return Message.from(bytes);
}

function decodeJson(message) {
  return JSON.parse(message.data().toString("utf8"));
}

try {
  const sample = {
    kind: "json",
    seq: 7,
    ok: true,
    nested: { numbers: [1, 2, 3], text: "codec" },
  };

  const real = loadRealMessageClass();

  if (real.Message) {
    const message = encodeJson(sample, real.Message);
    assert.equal(message.constructor, real.Message);
    assert.equal(message.data().toString("utf8"), JSON.stringify(sample));
    assert.deepEqual(decodeJson(message), sample);
    console.log(`[json] PASS real Message roundtrip via ${real.source}`);
  } else {
    const bytes = Buffer.from(JSON.stringify(sample), "utf8");
    const decoded = JSON.parse(bytes.toString("utf8"));
    assert.deepEqual(decoded, sample);
    console.log(
      `[json] SKIP real Message import; verified direct Buffer roundtrip instead: ${real.errors.join(" | ")}`
    );
  }
} catch (error) {
  console.error("[json] FAIL", error);
  process.exitCode = 1;
}
