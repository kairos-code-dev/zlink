// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');
test('router onReceive can send reply inside callback (callback+send)', async () => {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    router.bind('inproc://dealer-router-callback');
    dealer.connect('inproc://dealer-router-callback');
    const replyPromise = new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            reject(new Error('router callback+send timed out after 5000ms'));
        }, 5000);
        try {
            router.onReceive((routingId, parts) => {
                const payload = parts[0].toBuffer().toString();
                assert.equal(payload, 'request');
                assert.ok(Buffer.isBuffer(routingId));
                router.send(routingId, zlink.Message.copyOf('reply'));
            });
        }
        catch (error) {
            clearTimeout(timeout);
            reject(error);
            return;
        }
        dealer.onReceive((routingId, parts) => {
            clearTimeout(timeout);
            resolve({ routingId, parts });
        });
    });
    dealer.send(zlink.Message.copyOf('request'));
    const response = await replyPromise;
    assert.equal(response.routingId, null);
    assert.equal(response.parts[0].toBuffer().toString(), 'reply');
    dealer.close();
    router.close();
    ctx.close();
});
test('router onReceive can send multiple replies inside callback', async () => {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    router.bind('inproc://dealer-router-callback-multi');
    dealer.connect('inproc://dealer-router-callback-multi');
    const ROUND_COUNT = 5;
    let roundsCompleted = 0;
    const allRoundsPromise = new Promise((resolve, reject) => {
        const timeout = setTimeout(() => {
            reject(new Error(`multi-round callback+send timed out: completed ${roundsCompleted}/${ROUND_COUNT}`));
        }, 5000);
        try {
            router.onReceive((routingId, parts) => {
                const payload = parts[0].toBuffer().toString();
                assert.ok(payload.startsWith('request-'));
                assert.ok(Buffer.isBuffer(routingId));
                const index = payload.split('-')[1];
                router.send(routingId, zlink.Message.copyOf(`reply-${index}`));
            });
        }
        catch (error) {
            clearTimeout(timeout);
            reject(error);
            return;
        }
        dealer.onReceive((routingId, parts) => {
            const payload = parts[0].toBuffer().toString();
            assert.ok(payload.startsWith('reply-'));
            roundsCompleted += 1;
            if (roundsCompleted === ROUND_COUNT) {
                clearTimeout(timeout);
                resolve(roundsCompleted);
            }
        });
    });
    for (let i = 0; i < ROUND_COUNT; i += 1) {
        dealer.send(zlink.Message.copyOf(`request-${i}`));
    }
    const completed = await allRoundsPromise;
    assert.equal(completed, ROUND_COUNT);
    dealer.close();
    router.close();
    ctx.close();
});
test('router tryRecv + send works as synchronous request-reply', () => {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    router.bind('inproc://dealer-router-sync-rr');
    dealer.connect('inproc://dealer-router-sync-rr');
    dealer.send(zlink.Message.copyOf('sync-request'));
    const request = router.recv();
    assert.ok(Buffer.isBuffer(request.routingId));
    assert.equal(request.parts[0].toBuffer().toString(), 'sync-request');
    router.send(request.routingId, zlink.Message.copyOf('sync-reply'));
    const response = dealer.recv();
    assert.equal(response.parts[0].toBuffer().toString(), 'sync-reply');
    dealer.close();
    router.close();
    ctx.close();
});
