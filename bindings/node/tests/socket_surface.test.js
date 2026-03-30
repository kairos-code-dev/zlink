'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('canonical socket classes expose only directionally valid methods', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const xpub = new zlink.XPubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);
  const xsub = new zlink.XSubSocket(ctx);
  const pair = new zlink.PairSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const router = new zlink.RouterSocket(ctx);
  const stream = new zlink.StreamSocket(ctx);

  assert.equal(typeof pub.send, 'function');
  assert.equal(typeof pub.sendParts, 'function');
  assert.equal(pub.recv, undefined);
  assert.equal(pub.subscribe, undefined);

  assert.equal(typeof xpub.send, 'function');
  assert.equal(typeof xpub.sendParts, 'function');
  assert.equal(xpub.recv, undefined);
  assert.equal(xpub.subscribe, undefined);

  assert.equal(typeof sub.recv, 'function');
  assert.equal(typeof sub.subscribe, 'function');
  assert.equal(sub.send, undefined);
  assert.equal(sub.sendParts, undefined);

  assert.equal(typeof xsub.recv, 'function');
  assert.equal(typeof xsub.subscribe, 'function');
  assert.equal(xsub.send, undefined);
  assert.equal(xsub.sendParts, undefined);

  assert.equal(typeof pair.send, 'function');
  assert.equal(typeof pair.recv, 'function');
  assert.equal(pair.subscribe, undefined);

  assert.equal(typeof dealer.send, 'function');
  assert.equal(typeof dealer.recv, 'function');
  assert.equal(dealer.subscribe, undefined);

  assert.equal(typeof stream.send, 'function');
  assert.equal(typeof stream.recv, 'function');
  assert.equal(stream.subscribe, undefined);
  assert.equal(stream.streamAttach, undefined);

  assert.equal(typeof router.recv, 'function');
  assert.equal(typeof router.sendTo, 'function');
  assert.equal(typeof router.sendPartsTo, 'function');
  assert.equal(router.subscribe, undefined);
  assert.throws(() => router.send(zlink.Message.copyOf('bad')), /sendTo/);
  assert.throws(() => router.sendParts([zlink.Message.copyOf('bad')]), /sendPartsTo/);
  assert.throws(() => router.sendFrom(Buffer.from('bad'), 3), /sendTo/);

  stream.close();
  router.close();
  dealer.close();
  pair.close();
  xsub.close();
  sub.close();
  xpub.close();
  pub.close();
  ctx.close();
});
