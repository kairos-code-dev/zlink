// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist/canonical');
test('router can send reply in a synchronous request-reply exchange', () => {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    router.bind('inproc://dealer-router-callback');
    dealer.connect('inproc://dealer-router-callback');
    dealer.send('request');
    const request = router.recv();
    assert.ok(request.routingId instanceof zlink.RoutingId);
    assert.equal(request.parts[0].data().toString(), 'request');
    router.send(request.routingId, 'reply');
    const response = dealer.recv();
    assert.equal(response.parts[0].data().toString(), 'reply');
    dealer.close();
    router.close();
    ctx.close();
});
test('router can send multiple replies in a synchronous request-reply loop', () => {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    router.bind('inproc://dealer-router-callback-multi');
    dealer.connect('inproc://dealer-router-callback-multi');
    const ROUND_COUNT = 5;
    let roundsCompleted = 0;
    for (let i = 0; i < ROUND_COUNT; i += 1) {
        dealer.send(`request-${i}`);
        const request = router.recv();
        assert.ok(request.routingId instanceof zlink.RoutingId);
        assert.equal(request.parts[0].data().toString(), `request-${i}`);
        router.send(request.routingId, `reply-${i}`);
        const reply = dealer.recv();
        assert.equal(reply.parts[0].data().toString(), `reply-${i}`);
        roundsCompleted += 1;
    }
    assert.equal(roundsCompleted, ROUND_COUNT);
    dealer.close();
    router.close();
    ctx.close();
});
test('router recv + send works as synchronous request-reply', () => {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    router.bind('inproc://dealer-router-sync-rr');
    dealer.connect('inproc://dealer-router-sync-rr');
    dealer.send('sync-request');
    const request = router.recv();
    assert.ok(request.routingId instanceof zlink.RoutingId);
    assert.equal(request.parts[0].data().toString(), 'sync-request');
    router.send(request.routingId, 'sync-reply');
    const response = dealer.recv();
    assert.equal(response.parts[0].data().toString(), 'sync-reply');
    dealer.close();
    router.close();
    ctx.close();
});
