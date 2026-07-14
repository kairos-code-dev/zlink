const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');

test('TA-B1 sends a well-formed missing ActorRef through the framework caller', () => {
  const client = fs.readFileSync(path.join(
    nodeRoot,
    'e2e/ToActorMessaging/Client/main.ts'
  ), 'utf8');
  const actorServer = fs.readFileSync(path.join(
    nodeRoot,
    'e2e/ToActorMessaging/Server/Actor/main.ts'
  ), 'utf8');
  const scenario = client.slice(
    client.indexOf('async function runTaB1'),
    client.indexOf('async function runTaB2')
  );

  assert.match(scenario, /ensureActor\(options, 'ta-b1-reference'\)/);
  assert.match(scenario, /actors\/ta-b1-reference\/destroy/);
  assert.match(scenario, /const missingActor = reference\.actor/);
  assert.match(scenario, /assertFailure\([^;]+missingActor\s*\)/s);
  assert.doesNotMatch(scenario, /assertFailure\([^;]+true\)/s);
  assert.match(actorServer, /path: '\/actors\/ta-b1-reference\/ensure'/);
  assert.match(actorServer, /path: '\/actors\/ta-b1-reference\/destroy'/);
});
