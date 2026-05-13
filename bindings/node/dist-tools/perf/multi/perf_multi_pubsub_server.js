// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { createPayload, createRunId, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, pollEvents, pollEventHas, sendStopTokenWithRetry, trySocketPublish } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_PUBSUB');
    const pub = new zlink.PubSocket(ctx);
    const poller = new zlink.Poller();
    const payload = createPayload(options.msgSize);
    let rl = null;
    try {
        applySocketPolicy(pub, {
            noDrop: Number(process.env.PERF_MULTI_PUBSUB_XPUB_NODROP ?? 1) !== 0
        });
        pub.bind(options.endpoint);
        applyAutoHwmMsgUnit(pub, options.msgSize);
        ctx.recalculateAutoHwm();
        poller.add(pub, pollEvents(POLLOUT));
        console.log(`READY,${options.endpoint}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        for await (const line of rl) {
            if (line !== `START,${options.msgSize}`) {
                if (line === 'STOP' || line === 'QUIT') {
                    break;
                }
                continue;
            }
            const runId = createRunId(1);
            const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
            let seq = 1n;
            let pending = false;
            while (process.hrtime.bigint() < activeStopNs) {
                if (!pending) {
                    stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
                    if (trySocketPublish(pub, 'perf.topic', payload)) {
                        seq += 1n;
                        continue;
                    }
                    pending = true;
                }
                // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven `-1` wait.
                const ready = poller.wait(-1);
                if (!ready || !pollEventHas(ready, POLLOUT)) {
                    continue;
                }
                pending = false;
                if (process.hrtime.bigint() >= activeStopNs) {
                    break;
                }
            }
            // PERF_MULTI_TEST_POLICY § 1.3.1: emit wire-level stop token at phase
            // end so subscribers waiting on `wait(-1)` exit promptly.
            await sendStopTokenWithRetry(pub, (bytes) => trySocketPublish(pub, 'perf.topic', bytes));
            break;
        }
    }
    finally {
        rl?.close();
        poller.close();
        pub.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
