// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('../dist');
const ctx = new zlink.Context();
const pub = new zlink.PubSocket(ctx);
const sub = new zlink.SubSocket(ctx);
try {
    pub.bind('inproc://example-pubsub-recv');
    sub.connect('inproc://example-pubsub-recv');
    sub.setSubscription('topic');
    pub.publish('topic', zlink.Message.copyOf('payload'));
    const received = sub.subscribe();
    assert.equal(received.topic, 'topic');
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);
    console.log('pubsub recv sample ok');
}
finally {
    sub.close();
    pub.close();
    ctx.close();
}
