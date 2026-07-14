const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');
const pubSubRoot = path.join(nodeRoot, 'e2e/PubSub');

function read(relativePath) {
  return fs.readFileSync(path.join(pubSubRoot, relativePath), 'utf8');
}

test('PubSub provisions Redis and discovers fanout publishers through location rows', () => {
  const publisher = read('Server/Publisher/publisher-host-factory.ts');
  const subscriber = read('Server/Subscriber/subscriber-host-factory.ts');
  const runner = read('run_e2e.sh');
  const featureMap = read('feature-map.ko.md');

  for (const source of [publisher, subscriber]) {
    assert.doesNotMatch(source, /useInMemoryLocationStores/);
    assert.match(source, /addLocationStore\(createRedisLocationStore\(/);
    assert.match(source, /locationMessagingOptions\(\)/);
  }
  assert.match(subscriber, /enableSubscriber\(\)/);
  assert.doesNotMatch(subscriber, /enableSubscriber\(options\.publisherEndpoint\)/);

  assert.match(runner, /source "\$NODE_ROOT\/e2e\/redis-container\.sh"/);
  assert.match(runner, /start_redis_container/);
  assert.match(runner, /--redis-endpoint "\$REDIS_ENDPOINT"/);
  assert.match(runner, /--redis-key-prefix "\$REDIS_KEY_PREFIX"/);
  assert.match(featureMap, /Redis location store/);
});
