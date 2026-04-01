// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('../dist');
const FILTER_APPLIED = zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED;
const topic = 'spot:callback';
async function main() {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const monitor = spot.openMonitor(FILTER_APPLIED);
    try {
        const receivedPromise = new Promise((resolve, reject) => {
            try {
                spot.subscribeHandler((routingId, receivedTopic, parts) => {
                    resolve({ routingId, receivedTopic, parts });
                });
            }
            catch (error) {
                reject(error);
            }
        });
        const timeoutPromise = new Promise((_, reject) => {
            setTimeout(() => reject(new Error('spot callback sample timed out')), 5000);
        });
        spot.setSubscription(topic);
        while (true) {
            const event = monitor.recv();
            if (event.eventType === FILTER_APPLIED) {
                break;
            }
        }
        spot.publish(topic, zlink.Message.copyOf('spot-callback'));
        const received = await Promise.race([receivedPromise, timeoutPromise]);
        assert.ok(received.routingId === null || Buffer.isBuffer(received.routingId));
        assert.equal(received.receivedTopic, topic);
        assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['spot-callback']);
        console.log('spot callback sample ok');
    }
    finally {
        monitor.close();
        spot.close();
        node.close();
        ctx.close();
    }
    process.exit(0);
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
