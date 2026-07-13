#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMP_DIR"' EXIT

PACK_DIR="$TEMP_DIR/npm"
CONSUMER_DIR="$TEMP_DIR/consumer"
mkdir -p "$PACK_DIR" "$CONSUMER_DIR"

packages=(
  framework
  nestjs
  stream-connector
  framework-codec-protobuf
  framework-codec-msgpack
  framework-locations-redis
  stream-wire
)

expected=(
  '@zlink-systems/framework'
  '@zlink-systems/nestjs'
  '@zlink-systems/stream-connector'
  '@zlink-systems/framework-codec-protobuf'
  '@zlink-systems/framework-codec-msgpack'
  '@zlink-systems/framework-locations-redis'
  '@zlink-systems/stream-wire'
)

cd "$ROOT_DIR"
npm run build
for package_dir in "${packages[@]}"; do
  npm pack --silent "./packages/$package_dir" --pack-destination "$PACK_DIR" >/dev/null
done

node - "$PACK_DIR" "${expected[@]}" <<'NODE'
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const [packDir, ...expected] = process.argv.slice(2);
const names = fs.readdirSync(packDir).map((file) => {
  const json = execFileSync('tar', ['-xOf', path.join(packDir, file), 'package/package.json'], { encoding: 'utf8' });
  const manifest = JSON.parse(json);
  if (manifest.private === true) throw new Error(`${manifest.name} is still private`);
  return manifest.name;
}).sort();
const wanted = [...expected].sort();
if (JSON.stringify(names) !== JSON.stringify(wanted)) {
  throw new Error(`artifact manifest mismatch\nactual=${names.join(',')}\nexpected=${wanted.join(',')}`);
}
NODE

cd "$CONSUMER_DIR"
npm init -y >/dev/null
npm install --ignore-scripts \
  "$ROOT_DIR/../../../.artifacts/wsl/npm/zlink-systems-zlink-8.6.6.tgz" \
  "$PACK_DIR"/*.tgz >/dev/null
npm ls --all >/dev/null

cat > index.mjs <<'JS'
import * as framework from '@zlink-systems/framework';
import * as nestjs from '@zlink-systems/nestjs';
import * as connector from '@zlink-systems/stream-connector';
import * as protobuf from '@zlink-systems/framework-codec-protobuf';
import * as msgpack from '@zlink-systems/framework-codec-msgpack';
import * as redis from '@zlink-systems/framework-locations-redis';
import * as wire from '@zlink-systems/stream-wire';

for (const [name, module] of Object.entries({ framework, nestjs, connector, protobuf, msgpack, redis, wire })) {
  if (Object.keys(module).length === 0) throw new Error(`${name} package has no root exports`);
}
JS
node index.mjs

cat > index.ts <<'TS'
import type { ZLinkActorClient } from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { DefaultZlinkStreamConnector } from '@zlink-systems/stream-connector';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { zlinkMessagePackCodec } from '@zlink-systems/framework-codec-msgpack';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';

const options = zlinkFramework().codecs().use(zlinkProtobufCodec()).build();
void options;
void (undefined as ZLinkActorClient | undefined);
void ZLinkModule;
void DefaultZlinkStreamConnector;
void zlinkMessagePackCodec;
void ZLinkRedisLocationStore;
TS
"$ROOT_DIR/node_modules/.bin/tsc" --strict --noEmit --module node16 --moduleResolution node16 --target es2022 --skipLibCheck index.ts

echo "NODE_PACKAGED_CONTRACT_PASS packages=${#expected[@]}"
