// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, decodeMetricHeader, currentEpochNs, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { drainRecvSocket, waitForConnectionReady } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const subs = [];
    const collector = createMetricCollector({
        runId: 0,
        msgSize: options.msgSize
    });
    let stopResolve;
    const stopped = new Promise((resolve) => {
        stopResolve = resolve;
    });
    let stopCount = 0;
    let stop = false;
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const sub = new zlink.SubSocket(ctx);
            sub.setSubscription('perf.topic');
            subs.push(sub);
        }
        for (const sub of subs) {
            await waitForConnectionReady(sub, () => sub.connect(options.endpoint));
        }
        const recvTasks = subs.map((sub) => drainRecvSocket(sub, (received) => {
            const header = decodeMetricHeader(received.parts[0].data());
            if (!header) {
                return;
            }
            if (header.phase === 2) {
                stopCount += 1;
                if (stopCount >= options.clients) {
                    stopResolve();
                }
                return;
            }
            collector.record(header, currentEpochNs());
        }, () => stop));
        console.log('CLIENT_READY');
        await Promise.race([
            stopped,
            new Promise((resolve) => setTimeout(resolve, Math.ceil((options.warmup + options.duration + 2) * 1000)))
        ]);
        stop = true;
        await Promise.all(recvTasks);
        const result = await collector.finish();
        const resultLines = summarizeMetrics('MULTI_PUBSUB', 'tcp', options.msgSize, result.latenciesNs, options.duration);
        for (const line of resultLines) {
            console.log(line);
        }
    }
    finally {
        for (const sub of subs) {
            sub.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
