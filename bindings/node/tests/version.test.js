const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src/index.js');

const sleep = (ms) => {
  const sab = new SharedArrayBuffer(4);
  const arr = new Int32Array(sab);
  Atomics.wait(arr, 0, 0, ms);
};

let nativeOk = true;
try {
  zlink.version();
} catch (err) {
  nativeOk = false;
}

test('version matches core', { skip: !nativeOk }, () => {
  const v = zlink.version();
  assert.equal(v[0], 0);
  assert.equal(v[1], 7);
  assert.equal(v[2], 0);
});

test('pair send/recv', { skip: !nativeOk }, () => {
  const ctx = new zlink.Context();
  const s1 = new zlink.Socket(ctx, 0);
  const s2 = new zlink.Socket(ctx, 0);
  const endpoint = 'inproc://node-pair';
  s1.bind(endpoint);
  s2.connect(endpoint);
  const payload = Buffer.from('ping');
  for (let i = 0; i < 50; i++) {
    try {
      s1.send(payload, 0);
      break;
    } catch (err) {
      if (!String(err).includes('Resource temporarily unavailable')) throw err;
      sleep(10);
    }
  }
  const out = s2.recv(16, 0);
  assert.equal(out.toString(), 'ping');
  s1.close();
  s2.close();
  ctx.close();
});
