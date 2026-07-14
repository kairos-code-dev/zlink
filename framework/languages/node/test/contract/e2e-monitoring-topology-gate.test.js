const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const scenarioPath = path.resolve(
  __dirname,
  '../../e2e/RuntimeMonitoring/Client/Scenarios/mon-a2-location-runtime-events-scenario.ts'
);

test('MON-A2 changes provider membership and compares projection payloads', () => {
  const source = fs.readFileSync(scenarioPath, 'utf8');
  assert.match(source, /serviceBUrl, '\/shutdown'/);
  assert.match(source, /startServiceB\(options,/);
  assert.match(source, /TopologyChanged/);
  assert.match(source, /ServiceSummaryChanged/);
  assert.match(source, /removed\.topology < baseline\.topology/);
  assert.match(source, /restored\.topology >= baseline\.topology/);
  assert.match(source, /removed\.summary < baseline\.summary/);
  assert.match(source, /restored\.summary >= baseline\.summary/);
});
