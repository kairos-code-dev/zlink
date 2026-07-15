const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

function source(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8');
}

test('SF-D1 and SF-D2 restart an empty store without swallowing request failures', () => {
  const runner = source('e2e/DiscoveryRegistryHa/run_e2e.sh');
  const d1 = source('e2e/DiscoveryRegistryHa/Client/Scenarios/SfD1ShortOutageRecoveryScenario.ts');
  const d2 = source('e2e/DiscoveryRegistryHa/Client/Scenarios/SfD2LongOutageRecoveryScenario.ts');

  assert.doesNotMatch(runner, /docker (pause|unpause)/);
  assert.match(runner, /start_empty_redis/);
  assert.match(d1, /SF-D1 stop-redis/);
  assert.match(d1, /SF-D1 restart-redis/);
  assert.match(d2, /SF-D2 stop-redis-and-kill-api-b/);
  assert.match(d2, /SF-D2 restart-redis/);
  assert.doesNotMatch(d2, /catch\s*\{/);
  assert.doesNotMatch(d2, /maxGap\s*</);
});
