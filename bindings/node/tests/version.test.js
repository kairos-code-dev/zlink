const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src/index.js');

let nativeOk = true;
try {
  zlink.version();
} catch (err) {
  nativeOk = false;
}

test('version matches core', { skip: !nativeOk }, () => {
  const v = zlink.version();
  assert.equal(v[0], 0);
  assert.equal(v[1], 6);
  assert.equal(v[2], 0);
});
