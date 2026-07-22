#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const schema = JSON.parse(fs.readFileSync(path.join(root, "service-wire-v1.schema.json"), "utf8"));
const fixtures = JSON.parse(fs.readFileSync(path.join(root, "golden/service-decoder-fixtures-v1.json"), "utf8"));
const commands = new Map(schema.commands.map((entry) => [entry.id, entry]));

function fail(code) {
  const error = new Error(code);
  error.code = code;
  throw error;
}

function decode(bytes) {
  if (bytes.length < schema.protocol.headPrefixBytes) fail("truncated-head");
  if (bytes[0] !== schema.protocol.magic[0] || bytes[1] !== schema.protocol.magic[1]) fail("invalid-magic");
  if (bytes[2] !== schema.protocol.wireMajor) fail("invalid-major");
  const command = commands.get(bytes[3]);
  if (!command) fail("unknown-command");
  const flags = bytes[4];
  const allowed = command.allowedFlags.reduce((value, name) => {
    const flag = schema.flags.find((entry) => entry.name === name);
    return value | flag.bit;
  }, 0);
  if ((flags & ~allowed) !== 0) fail("forbidden-flag");
  if (command.name === "livenessProbe" || command.name === "livenessAck") {
    if (bytes.length < 13) fail("truncated-field");
    if (bytes.length > 13) fail("trailing-byte");
    let probeId = 0n;
    for (const byte of bytes.slice(5)) probeId = (probeId << 8n) | BigInt(byte);
    if (probeId === 0n) fail("invalid-field");
    return { command: command.name, probeId };
  }
  return { command: command.name };
}

for (const fixture of fixtures.canonical) {
  const decoded = decode(fixture.bytes);
  if (decoded.command !== fixture.name || decoded.probeId.toString() !== fixtures.probeId) {
    throw new Error(`canonical fixture mismatch: ${fixture.name}`);
  }
}
for (const fixture of fixtures.malformed) {
  try {
    decode(fixture.bytes);
    throw new Error(`malformed fixture was accepted: ${fixture.name}`);
  } catch (error) {
    if (error.code !== fixture.error) throw error;
  }
}
const probe = decode(fixtures.canonical.find((entry) => entry.name === "livenessProbe").bytes);
const ack = decode(fixtures.canonical.find((entry) => entry.name === "livenessAck").bytes);
if (probe.probeId !== ack.probeId) throw new Error("livenessAck does not echo livenessProbe id");

console.log(`service wire decoder fixtures valid: canonical=${fixtures.canonical.length} malformed=${fixtures.malformed.length} probeEcho=pass`);
