const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');
const pubSubRoot = path.join(nodeRoot, 'e2e/PubSub');

function read(relativePath) {
  return fs.readFileSync(path.join(pubSubRoot, relativePath), 'utf8');
}

test('PubSub keeps classic fanout independent from RouteMesh location discovery', () => {
  const publisher = read('Server/Publisher/publisher-host-factory.ts');
  const subscriber = read('Server/Subscriber/subscriber-host-factory.ts');
  const runner = read('run_e2e.sh');
  const featureMap = read('feature-map.ko.md');

  for (const source of [publisher, subscriber]) {
    assert.doesNotMatch(source, /useInMemoryLocationStores/);
    assert.doesNotMatch(source, /addLocationStore\(createRedisLocationStore\(/);
    assert.doesNotMatch(source, /locationMessagingOptions\(\)/);
  }
  assert.match(subscriber, /enableSubscriber\(options\.publisherEndpoint\)/);

  assert.doesNotMatch(runner, /start_redis_container/);
  assert.doesNotMatch(runner, /--redis-endpoint "\$REDIS_ENDPOINT"/);
  assert.doesNotMatch(runner, /--redis-key-prefix "\$REDIS_KEY_PREFIX"/);
  assert.match(featureMap, /classic fanout/);
});
