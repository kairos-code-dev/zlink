// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { attachCallbackCollector, driveSender, finishCollector } = require('./perf_single_common');
async function waitSpotFilterApplied(spot, topic) {
    const monitor = spot.openMonitor(zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);
    try {
        spot.setSubscription(topic);
        while (true) {
            const event = monitor.recv();
            if (event.eventType === zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED) {
                return;
            }
        }
    }
    finally {
        monitor.close();
    }
}
async function runSpotBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const topic = 'perf:spot';
    try {
        const state = attachCallbackCollector((handler) => spot.subscribeHandler(handler), msgSize, options, (_, __, parts) => parts[0].toBuffer());
        await waitSpotFilterApplied(spot, topic);
        await driveSender((payload) => (spot.tryPublish(topic, payload) === zlink.SendResult.Sent), state);
        return await finishCollector(state);
    }
    finally {
        spot.close();
        node.close();
        ctx.close();
    }
}
module.exports = { runSpotBenchmark };
