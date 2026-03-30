'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('spot exposes unified publish and subscribe surface', () => {
  const ctx = new zlink.Context();
  const spot = new zlink.Spot(ctx);
  const sub = new zlink.SubSocket(ctx);
  const monitor = spot.openMonitor();

  spot.subscribe('topic');
  spot.subscribePattern('topic.*');
  spot.unsubscribe('topic');
  sub.subscribe('topic');
  sub.unsubscribe('topic');

  assert.equal(typeof monitor.recv, 'function');

  sub.close();
  monitor.close();
  spot.close();
  ctx.close();
});

test('canonical pub/sub surface hides opposite-direction methods', () => {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  assert.equal(pub.recv, undefined);
  assert.equal(sub.send, undefined);

  sub.close();
  pub.close();
  ctx.close();
});
