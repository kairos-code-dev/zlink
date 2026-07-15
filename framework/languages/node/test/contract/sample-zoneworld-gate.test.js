const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');

function read(relativePath) {
  return fs.readFileSync(path.join(nodeRoot, relativePath), 'utf8');
}

test('ZoneWorld Node sample contains the language server and headless scenario client', () => {
  assert.ok(fs.existsSync(path.join(nodeRoot, 'samples/ZoneWorld/package.json')));
  assert.ok(fs.existsSync(path.join(nodeRoot, 'samples/ZoneWorld/Server/Gateway/main.ts')));
  assert.ok(fs.existsSync(path.join(nodeRoot, 'samples/ZoneWorld/Server/ZoneNode/main.ts')));
  assert.ok(fs.existsSync(path.join(nodeRoot, 'samples/ZoneWorld/Server/Ops/main.ts')));
  assert.ok(fs.existsSync(path.join(nodeRoot, 'samples/ZoneWorld/Client/main.ts')));
});

test('ZoneNode shares one automatic routing id across all three transport members', () => {
  const module = read('samples/ZoneWorld/Server/ZoneNode/zone-node-module.ts');
  for (const member of ['zoneworld.zones', 'zoneworld.bridge', 'zoneworld.report']) {
    assert.match(module, new RegExp(`(?:addSpotMesh|addRouteMeshChannel|addClientServerChannel)\\([^)]*${member.replace('.', '\\.')}[^)]*\\)[\\s\\S]*?useAllocatedRoutingId\\(2, 'zn'\\)[\\s\\S]*?setRoutingIdAllocationGroup\\('zoneworld\\.zone-node'\\)`));
  }
  assert.doesNotMatch(module, /\.routingId\(|enableRouter\([^\n]+,[^\n]+\)/);
});

test('ZoneWorld runner proves the canonical scenario and routing-id gates', () => {
  const runner = read('samples/ZoneWorld/Runner/sample-runner.mjs');
  for (const marker of [
    'topology=ready',
    'zoneworld-transfer=completed',
    'zoneworld-border-sync=completed',
    'zoneworld-ops-observe=completed',
    'zoneworld-ops-announce=completed',
    'zoneworld-ops-maintenance=completed',
    'zoneworld=completed'
  ]) assert.match(runner, new RegExp(marker));
  for (const gate of ['ZW-G1', 'ZW-G2', 'ZW-G3', 'ZW-G4', 'ZW-G5']) {
    assert.match(runner, new RegExp(gate));
  }
  assert.match(runner, /WaitingForSlot/);
  assert.match(runner, /generation/);
  assert.match(runner, /lease/);
});
