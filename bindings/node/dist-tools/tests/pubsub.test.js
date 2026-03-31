'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');
test('spot exposes unified publish and subscribe surface', () => {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const sub = new zlink.SubSocket(ctx);
    const monitor = spot.openMonitor();
    spot.setSubscription('topic');
    spot.unsetSubscription('topic');
    sub.setSubscription('topic');
    sub.unsetSubscription('topic');
    assert.equal(typeof monitor.recv, 'function');
    sub.close();
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
});
test('spot trySubscribe receives published payload after one immediate turn', async () => {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const topic = 'spot:direct';
    const monitor = spot.openMonitor(zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);
    spot.setSubscription(topic);
    while (true) {
        const event = monitor.recv();
        if (event.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
            break;
        }
    }
    spot.publish(topic, zlink.Message.copyOf('payload'));
    await new Promise((resolve) => setImmediate(resolve));
    const received = spot.trySubscribe();
    assert.notEqual(received, null);
    assert.equal(received.topic, topic);
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
});
test('spot subscribeHandler delivers callback payloads', async () => {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const topic = 'spot:callback';
    const monitor = spot.openMonitor(zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);
    const received = await new Promise((resolve, reject) => {
        try {
            spot.subscribeHandler((routingId, receivedTopic, parts) => {
                resolve({ routingId, receivedTopic, parts });
            });
        }
        catch (error) {
            reject(error);
            return;
        }
        spot.setSubscription(topic);
        while (true) {
            const event = monitor.recv();
            if (event.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
                break;
            }
        }
        spot.publish(topic, zlink.Message.copyOf('payload'));
    });
    assert.ok(received.routingId === null || Buffer.isBuffer(received.routingId));
    assert.equal(received.receivedTopic, topic);
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);
    monitor.close();
    spot.close();
    node.close();
    ctx.close();
});
test('canonical pub/sub surface hides opposite-direction methods', () => {
    const ctx = new zlink.Context();
    const pub = new zlink.PubSocket(ctx);
    const sub = new zlink.SubSocket(ctx);
    assert.equal(pub.receive, undefined);
    assert.equal(typeof pub.tryPublish, 'function');
    assert.equal(pub.send, undefined);
    assert.equal(sub.send, undefined);
    assert.equal(typeof sub.subscribe, 'function');
    sub.close();
    pub.close();
    ctx.close();
});
test('sub sockets receive Subscribed domain objects and TrySubscribe returns null when empty', () => {
    const ctx = new zlink.Context();
    const pub = new zlink.PubSocket(ctx);
    const sub = new zlink.SubSocket(ctx);
    pub.bind('inproc://subscribed-contract');
    sub.connect('inproc://subscribed-contract');
    sub.setSubscription('topic');
    assert.equal(sub.trySubscribe(), null);
    pub.publish('topic', zlink.Message.copyOf('payload'));
    const received = sub.subscribe();
    assert.equal(received.topic, 'topic');
    assert.equal(received.routingId, null);
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);
    sub.close();
    pub.close();
    ctx.close();
});
test('subscribeHandler delivers topic-aware multipart payloads', async () => {
    const ctx = new zlink.Context();
    const pub = new zlink.PubSocket(ctx);
    const sub = new zlink.SubSocket(ctx);
    pub.bind('inproc://subscribe-handler-contract');
    sub.connect('inproc://subscribe-handler-contract');
    sub.setSubscription('topic');
    const received = await new Promise((resolve, reject) => {
        try {
            sub.subscribeHandler((routingId, topic, parts) => {
                resolve({ routingId, topic, parts });
            });
        }
        catch (err) {
            reject(err);
            return;
        }
        pub.publish('topic', zlink.Message.copyOf('payload'));
    });
    assert.equal(received.routingId, null);
    assert.equal(received.topic, 'topic');
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['payload']);
    sub.close();
    pub.close();
    ctx.close();
});
