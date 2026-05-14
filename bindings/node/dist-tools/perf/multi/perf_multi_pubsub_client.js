// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, summarizeMetrics } = require('../common/perf_metrics');
const { configureTlsClient } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, emitMultiSocketHwmDetail, pollEvents, subscribeNoWait, waitForConnectionReady } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_PUBSUB');
    const subs = [];
    let rl = null;
    let collector = null;
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const sub = new zlink.SubSocket(ctx);
            applySocketPolicy(sub);
            configureTlsClient(sub, options.transport);
            sub.setSubscription('perf.topic');
            await waitForConnectionReady(sub, () => sub.connect(options.endpoint));
            applyAutoHwmMsgUnit(sub, options.msgSize);
            subs.push(sub);
        }
        ctx.recalculateAutoHwm();
        for (const sub of subs) {
            emitMultiSocketHwmDetail(sub, 'endpoint', options.transport, options.msgSize);
        }
        console.log(`CLIENT_READY,${options.msgSize}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line === `START,${options.msgSize}`) {
                const activeStartNs = currentEpochNs();
                const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
                collector = createMetricCollector({
                    runId: createRunId(1),
                    msgSize: options.msgSize,
                    activeStartNs,
                    activeStopNs,
                });
                const poller = new zlink.Poller();
                const completionTimer = new zlink.Timer();
                try {
                    for (let i = 0; i < subs.length; i += 1) {
                        poller.add(subs[i], pollEvents(POLLIN), i);
                    }
                    completionTimer.start(BigInt(Math.floor((options.duration + 2) * 1_000_000_000)), 1n);
                    poller.add(completionTimer, 'completion');
                    let cooldownSeen = false;
                    while (!cooldownSeen && currentEpochNs() < activeStopNs + 2000000000n) {
                        const ready = poller.waitMany(poller.size, -1);
                        if (!ready || ready.length === 0) {
                            continue;
                        }
                        for (const event of ready) {
                            if (event.timer === completionTimer) {
                                completionTimer.recv();
                                cooldownSeen = true;
                                break;
                            }
                            const index = event.tag ?? event.userData;
                            if (!Number.isInteger(index)) {
                                continue;
                            }
                            while (true) {
                                const received = subscribeNoWait(subs[index]);
                                if (!received) {
                                    break;
                                }
                                try {
                                    const header = decodeMetricHeaderFromParts(received.parts);
                                    if (header && header.phase === 2) {
                                        cooldownSeen = true;
                                        continue;
                                    }
                                    if (currentEpochNs() < activeStopNs) {
                                        collector.record(header, currentEpochNs());
                                    }
                                }
                                finally {
                                    received.close();
                                }
                            }
                        }
                    }
                }
                finally {
                    poller.remove(completionTimer);
                    completionTimer.stop();
                    completionTimer.close();
                    poller.close();
                }
                break;
            }
        }
        const result = collector ? await collector.finish() : { latenciesNs: [] };
        for (const line of summarizeMetrics('MULTI_PUBSUB', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(line);
        }
        console.log(`CLIENT_DONE,${options.msgSize}`);
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
