const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src/index.js');

let nativeOk = true;
try {
  zlink.version();
} catch (err) {
  nativeOk = false;
}

test('construct/close service objects', { skip: !nativeOk }, () => {
  const ctx = new zlink.Context();
  const reg = new zlink.Registry(ctx);
  const disc = new zlink.Discovery(ctx, zlink.SERVICE_TYPE_GATEWAY_RECEIVER);
  const gw = new zlink.Gateway(ctx, disc);
  const provider = new zlink.Receiver(ctx);
  const node = new zlink.SpotNode(ctx);
  const spot = new zlink.Spot(node);

  spot.close();
  node.close();
  provider.close();
  gw.close();
  disc.close();
  reg.close();
  ctx.close();

  assert.ok(true);
});
