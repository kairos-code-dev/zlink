'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src');

test('spot exposes unified publish and subscribe surface', () => {
  const ctx = new zlink.Context();
  const spot = new zlink.Spot(ctx);
  const sub = new zlink.Socket(ctx, zlink.SocketType.SUB);
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
