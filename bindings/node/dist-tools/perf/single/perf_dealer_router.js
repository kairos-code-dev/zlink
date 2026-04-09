// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { drainRecvSocket, waitForConnectionReady } = require('./perf_single_common');
async function runDealerRouterBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const router = new zlink.RouterSocket(ctx);
    const dealer = new zlink.DealerSocket(ctx);
    const endpoint = `inproc://perf-dealer-router-${process.pid}-${msgSize}`;
    try {
        router.bind(endpoint);
        await waitForConnectionReady(dealer, () => dealer.connect(endpoint));
        const startedAtNs = process.hrtime.bigint();
        const runId = createRunId();
        const collector = createMetricCollector({ runId, msgSize });
        const payload = createPayload(msgSize);
        let seq = 1n;
        const warmupUntilNs = startedAtNs
            + BigInt(Math.floor(options.warmup * 1_000_000_000));
        const stopAtNs = startedAtNs
            + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));
        let stop = false;
        const recvTask = drainRecvSocket(router, (received) => {
            const header = decodeMetricHeader(received.parts[0].data);
            collector.record(header, currentEpochNs());
        }, () => stop);
        while (process.hrtime.bigint() < stopAtNs) {
            for (let i = 0; i < 256 && process.hrtime.bigint() < stopAtNs; i += 1) {
                stampPayload(payload, {
                    phase: process.hrtime.bigint() < warmupUntilNs ? 0 : 1,
                    runId,
                    msgSize,
                    seq
                });
                if (dealer.trySend(payload) !== zlink.SendResult.Sent) {
                    break;
                }
                seq += 1n;
            }
            if ((Number(seq) & 0x03) === 0) {
                await sleepImmediate();
            }
        }
        for (let i = 0; i < 4; i += 1) {
            await sleepImmediate();
        }
        stop = true;
        await recvTask;
        const result = await collector.finish();
        return result.latenciesNs;
    }
    finally {
        dealer.close();
        router.close();
        ctx.close();
    }
}
module.exports = { runDealerRouterBenchmark };
