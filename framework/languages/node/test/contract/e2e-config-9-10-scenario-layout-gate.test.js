import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';

const root = path.resolve(import.meta.dirname, '../..');

test('Config 9 and 10 keep one client scenario file per documented id', () => {
  assertScenarioLayout('ToActorMessaging', ids('TA', { A: 4, B: 3 }));
  assertScenarioLayout('SpotActorTransfer', ids('ST', { A: 3, B: 4, C: 3, D: 2, E: 2, F: 6 }));
});

function assertScenarioLayout(configName, expectedIds) {
  const client = path.join(root, 'e2e', configName, 'Client');
  const scenarioDirectory = path.join(client, 'Scenarios');
  const supportDirectory = path.join(client, 'Support');
  assert.equal(fs.existsSync(scenarioDirectory), true, `${configName} Scenarios directory`);
  assert.equal(fs.existsSync(supportDirectory), true, `${configName} Support directory`);

  const files = fs.readdirSync(scenarioDirectory)
    .filter((name) => name.endsWith('-scenario.ts'))
    .sort();
  assert.equal(files.length, expectedIds.length, `${configName} scenario file count`);

  const actualIds = files.map((name) => {
    const source = fs.readFileSync(path.join(scenarioDirectory, name), 'utf8');
    const matches = [...new Set(source.match(/\b(?:TA|ST)-[A-Z][0-9]+\b/g) ?? [])];
    assert.equal(matches.length, 1, `${configName}/${name} scenario id`);
    assert.match(source.split(/\r?\n/, 1)[0], new RegExp(`^// ${matches[0]}: `));
    assert.match(source, /export async function run/);
    return matches[0];
  }).sort();
  assert.deepEqual(actualIds, expectedIds);

  const main = fs.readFileSync(path.join(client, 'main.ts'), 'utf8');
  assert.doesNotMatch(main, /(?:async\s+)?function\s+run(?:Ta)?[A-F][0-9]+\s*\(/);
}

function ids(prefix, tracks) {
  return Object.entries(tracks)
    .flatMap(([track, count]) => Array.from({ length: count }, (_, index) => `${prefix}-${track}${index + 1}`))
    .sort();
}
