// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../..');
const { createMetricCollector, createRunId, decodeMetricHeader, currentEpochNs, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, drainRecvSocket, resolveMultiLatencySampleCap } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_PUBSUB');
    const subs = [];
    let rl = null;
    let collector = null;
    let stop = false;
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const sub = new zlink.SubSocket(ctx);
            applySocketPolicy(sub);
            sub.setSubscription('perf.topic');
            sub.connect(options.endpoint);
            applyAutoHwmMsgUnit(sub, options.msgSize);
            subs.push(sub);
        }
        ctx.recalculateAutoHwm();
        const recvTasks = subs.map((sub) => drainRecvSocket(sub, (received) => {
            if (!collector) {
                return;
            }
            collector.record(decodeMetricHeader(received.parts[0].data()), currentEpochNs());
        }, () => stop));
        console.log(`CLIENT_READY,${options.msgSize}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === `PHASE_ACTIVE,${options.msgSize}`) {
                const activeStartNs = currentEpochNs();
                const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
                collector = createMetricCollector({
                    runId: createRunId(1),
                    msgSize: options.msgSize,
                    activeStartNs,
                    activeStopNs,
                    sampleCap: resolveMultiLatencySampleCap()
                });
                await new Promise((resolve) => {
                    const remainMs = Number((activeStopNs - currentEpochNs()) / 1000000n);
                    if (remainMs > 0) {
                        setTimeout(resolve, remainMs);
                    }
                    else {
                        resolve();
                    }
                });
                stop = true;
                for (const sub of subs) {
                    try {
                        sub.close();
                    }
                    catch (err) {
                        console.error(`[perf] close failed: ${err}`);
                    }
                }
                break;
            }
        }
        await Promise.all(recvTasks);
        const result = collector ? await collector.finish() : { latenciesNs: [] };
        for (const line of summarizeMetrics('MULTI_PUBSUB', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(line);
        }
    }
    finally {
        rl?.close();
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
