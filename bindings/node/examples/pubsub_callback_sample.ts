// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const zlink = require('../dist');

async function main() {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  try {
    pub.bind('inproc://example-pubsub-callback');
    sub.connect('inproc://example-pubsub-callback');
    sub.setSubscription('topic');

    const received = await new Promise((resolve, reject) => {
      try {
        sub.subscribeHandler((routingId, topic, parts) => {
          resolve({ routingId, topic, parts });
        });
      } catch (error) {
        reject(error);
        return;
      }
      pub.publish('topic', zlink.Message.copyOf('payload'));
    });

    assert.equal(received.routingId, null);
    assert.equal(received.topic, 'topic');
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);
    console.log('pubsub callback sample ok');
  } finally {
    sub.close();
    pub.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
