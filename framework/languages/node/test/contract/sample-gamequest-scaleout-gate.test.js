const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');

function read(relativePath) {
  return fs.readFileSync(path.join(nodeRoot, relativePath), 'utf8');
}

test('GameQuest proves concurrent players execute on different owners', () => {
  const client = read('samples/GameQuest.Ts/Client/gamequest-client-scenario.ts');
  const store = read('samples/GameQuest.Ts/Server/Shared/Store/quest-progress-store.ts');

  assert.match(client, /await Promise\.all\(\[/);
  for (const evidence of [
    'owner:mission-a:player-alice',
    'owner:mission-b:player-bob'
  ]) {
    assert.match(store, new RegExp(`'${evidence}'`));
    assert.match(client, new RegExp(`assertion\\.evidence\\.includes\\('${evidence}'\\)`));
  }
});
