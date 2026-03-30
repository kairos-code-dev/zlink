// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createPayload, latencyUsFromPayload, stampPayload } = require('../common/perf_metrics');
async function runSpotBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const spot = new zlink.Spot(ctx);
    const topic = 'perf:spot';
    const payload = createPayload(msgSize);
    const latenciesUs = [];
    let done = false;
    const startedAt = process.hrtime.bigint();
    const warmupNs = BigInt(Math.floor(options.warmup * 1_000_000_000));
    const activeNs = BigInt(Math.floor(options.duration * 1_000_000_000));
    const monitor = spot.openMonitor(zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED);
    try {
        spot.subscribeHandler((_, __, parts) => {
            const elapsed = process.hrtime.bigint() - startedAt;
            if (elapsed >= warmupNs && elapsed < warmupNs + activeNs) {
                latenciesUs.push(latencyUsFromPayload(parts[0].toBuffer()));
            }
            if (elapsed >= warmupNs + activeNs) {
                done = true;
            }
        });
        spot.setSubscription(topic);
        monitor.recv();
        while (!done) {
            stampPayload(payload);
            const result = spot.tryPublish(topic, payload);
            if (result !== zlink.SendResult.Sent) {
                await new Promise((resolve) => setImmediate(resolve));
                continue;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        return latenciesUs;
    }
    finally {
        monitor.close();
        spot.close();
        ctx.close();
    }
}
module.exports = { runSpotBenchmark };
