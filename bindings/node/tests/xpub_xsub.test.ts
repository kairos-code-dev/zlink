'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');

function recvSubscriptionEventMaybe(socket) {
  try {
    return socket.receiveSubscriptionEvent(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

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

  assert.equal(typeof xsub.options.topicsCount, 'number');

  const event = xpub.receiveSubscriptionEvent();
  assert.equal(event.topic, 'topic');
  assert.equal(event.subscribed, true);
  assert.equal(recvSubscriptionEventMaybe(xpub), null);

  xsub.close();
  xpub.close();
  ctx.close();
});
