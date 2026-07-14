const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../../e2e/ResilienceLifecycle');
const read = (relative) => fs.readFileSync(path.join(root, relative), 'utf8');

test('RL-D1 drives actual fanout through many subscribers', () => {
  const scenario = read('Client/Scenarios/rl-d1-high-fanout-scenario.ts');
  const provider = read('Server/Provider/provider-host-factory.ts');
  const consumer = read('Server/Consumer/consumer-host-factory.ts');
  const runner = read('run_e2e.sh');

  assert.match(provider, /addFanoutChannel\(ResilienceNames\.fanoutChannel\)/);
  assert.match(provider, /enablePublisher\(options\.fanoutEndpoint\)/);
  assert.match(consumer, /enableSubscriber\(\)/);
  assert.match(consumer, /addPublishHandler\(PacketNames\.loadEvent, LoadEventHandler\)/);
  assert.match(runner, /RL_D1_SUBSCRIBER_COUNT/);
  assert.match(runner, /--fanout-subscriber-urls/);
  assert.match(scenario, /options\.fanoutSubscriberUrls/);
  assert.match(scenario, /\/fanout\/publish/);
  assert.doesNotMatch(scenario, /\/profile\/request/);
});
