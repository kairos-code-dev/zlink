const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

test('MON-A4 separates replacement, crash failover, and transport weight exclusion', () => {
  const runner = read('e2e/RuntimeMonitoring/run_e2e.sh');
  const client = read('e2e/RuntimeMonitoring/Client/main.ts');
  const scenario = read('e2e/RuntimeMonitoring/Client/Scenarios/mon-a4-availability-transition-scenario.ts');
  const recorder = read('e2e/RuntimeMonitoring/Server/Service/Handlers/service-handlers.ts');

  assert.match(runner, /svc-b-replacement\.config\.json/);
  assert.match(runner, /--rid svc-b[\s\S]+--channel-endpoint "\$CHANNEL_B_REPLACEMENT_ENDPOINT"/);
  assert.match(runner, /--replacement-service-url "\$SVC_B_REPLACEMENT_URL"/);
  assert.match(runner, /--replacement-service-config "\$CONFIG_DIR\/svc-b-replacement\.config\.json"/);
  assert.match(client, /replaceServiceBProcess\(\(\) => runMonA4\(options\)\)/);
  assert.match(client, /await previous\?\.stop\(\)/);
  assert.match(runner, /RESERVED_PORTS/);
  assert.match(scenario, /startReplacementService\(options/);
  assert.match(scenario, /beforeFailover\.endpoint !== afterFailover\.endpoint/);
  assert.match(scenario, /kind=TopologyChanged/);
  assert.match(scenario, /kind=connectionReady/);
  assert.match(scenario, /kind=disconnected/);
  assert.match(scenario, /SIGKILL/);
  assert.match(scenario, /lease[\s\S]*follow-up/);
  assert.match(scenario, /\/admin\/exclude/);
  assert.match(scenario, /action=exclude/);
  assert.doesNotMatch(scenario, /service drain evidence/);
  assert.match(recorder, /topologyNodes=/);
});

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8');
}
