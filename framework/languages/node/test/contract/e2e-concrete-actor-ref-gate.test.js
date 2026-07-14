const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

test('P0 actor-ref E2E responses require non-empty node and positive generation', () => {
  const toActor = fs.readFileSync(path.join(root, 'e2e/ToActorMessaging/Client/main.ts'), 'utf8');
  const spotService = fs.readFileSync(path.join(
    root,
    'e2e/SpotService/Client/Scenarios/sm-d15-scenario.ts'
  ), 'utf8');

  assert.match(toActor, /assertConcreteActorRef\(response\.actor/);
  assert.match(toActor, /BigInt\(actor\.generation\) > 0n/);
  assert.match(toActor, /actor\.nodeRid\.trim\(\)\.length > 0/);
  assert.match(spotService, /auth\.generation !== undefined && BigInt\(auth\.generation\) > 0n/);
  assert.match(spotService, /auth\.nodeRid\.trim\(\)\.length > 0/);
});
