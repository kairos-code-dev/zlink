const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

test('MON-A1 correlates socket transitions by remote address and routing id', () => {
  const scenario = fs.readFileSync(path.join(
    root,
    'e2e/RuntimeMonitoring/Client/Scenarios/mon-a1-socket-events-scenario.ts'
  ), 'utf8');

  assert.match(scenario, /parseSocketEvidence/);
  assert.match(scenario, /remoteAddr\.startsWith\('tcp:\/\/'\)/);
  assert.match(scenario, /routingId !== '<null>'/);
  assert.match(scenario, /event\.remoteAddr === disconnected\.remoteAddr/);
  assert.match(scenario, /event\.routingId === disconnected\.routingId/);
});
