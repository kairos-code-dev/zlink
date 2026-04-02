'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('xpub exposes subscription events and dedicated option helpers', () => {
  const ctx = new zlink.Context();
  const xpub = new zlink.XPubSocket(ctx);
  const xsub = new zlink.XSubSocket(ctx);

  xpub.bind('inproc://xpub-events');
  xsub.connect('inproc://xpub-events');
  xpub.options.verbose = true;
  xpub.options.verboser = true;
  xpub.options.noDrop = true;
  xsub.setSubscription('topic');

  assert.equal(zlink.SocketOption.XPUB_VERBOSE, 0x3301);
  assert.equal(typeof xsub.options.topicsCount, 'number');

  const event = xpub.receiveSubscriptionEvent();
  assert.equal(event.topic, 'topic');
  assert.equal(event.subscribed, true);
  assert.equal(xpub.tryReceiveSubscriptionEvent(), null);

  xsub.close();
  xpub.close();
  ctx.close();
});
