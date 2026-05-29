import assert from "node:assert/strict";
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const codec = require("./dist/index.js");

try {
  const sample = {
    kind: "json",
    seq: 7,
    ok: true,
    nested: { numbers: [1, 2, 3], text: "codec" },
  };

  const message = codec.encode(sample);
  assert.equal(message.constructor.name, "Message");
  assert.equal(message.data().toString("utf8"), JSON.stringify(sample));
  assert.deepEqual(codec.decode(message), sample);
  console.log("[json] PASS codec Message roundtrip");
} catch (error) {
  console.error("[json] FAIL", error);
  process.exitCode = 1;
}
