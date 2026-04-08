// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, sleepImmediate, stampPayload } = require('../common/perf_metrics');
async function runPairBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const server = new zlink.PairSocket(ctx);
    const client = new zlink.PairSocket(ctx);
    const endpoint = `inproc://perf-pair-${process.pid}-${msgSize}`;
    try {
        server.bind(endpoint);
        client.connect(endpoint);
        const startedAtNs = process.hrtime.bigint();
        const runId = createRunId();
        const collector = createMetricCollector({ runId, msgSize });
        const payload = createPayload(msgSize);
        const warmupUntilNs = startedAtNs
            + BigInt(Math.floor(options.warmup * 1_000_000_000));
        const stopAtNs = startedAtNs
            + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));
        server.onReceive((_, parts) => {
            const messageBuffer = parts[0].data;
            const header = decodeMetricHeader(messageBuffer);
            collector.record(header, process.hrtime.bigint());
        });
        let turns = 0;
        while (process.hrtime.bigint() < stopAtNs) {
            for (let i = 0; i < 256 && process.hrtime.bigint() < stopAtNs; i += 1) {
                stampPayload(payload, {
                    phase: process.hrtime.bigint() < warmupUntilNs ? 2 : 0,
                    runId,
                    msgSize
                });
                if (client.trySend(payload) !== zlink.SendResult.Sent) {
                    break;
                }
            }
            turns += 1;
            if ((turns & 0x03) === 0) {
                await sleepImmediate();
            }
        }
        const result = await collector.finish();
        return result.latenciesUs;
    }
    finally {
        client.close();
        server.close();
        ctx.close();
    }
}
module.exports = { runPairBenchmark };
