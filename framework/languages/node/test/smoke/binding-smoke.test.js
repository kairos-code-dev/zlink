const assert = require('node:assert/strict');
const test = require('node:test');

const zlink = require('../../../../../bindings/node/dist');

test('binding public API exposes version', () => {
  const actual = zlink.version();
  assert.equal(Array.isArray(actual), true);
  assert.equal(actual.length, 3);
  assert.equal(actual.every((part) => Number.isInteger(part)), true);
});

test('framework adapter reads binding version through public API', () => {
  const framework = require('../../packages/framework/dist');
  const info = framework.getNodeBindingInfo();
  assert.deepEqual(info.version, zlink.version());
});
