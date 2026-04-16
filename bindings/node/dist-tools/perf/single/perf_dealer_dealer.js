// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { benchmarkEndpoint, drainRecvNow, drainRecvSocket, waitForConnectionReady, trySocketSend } = require('./perf_single_common');
async function runDealerDealerBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const server = new zlink.DealerSocket(ctx);
    const client = new zlink.DealerSocket(ctx);
    const endpoint = await benchmarkEndpoint(options.transport, `dealer-dealer-${msgSize}`);
    try {
        server.bind(endpoint);
        await waitForConnectionReady(client, () => client.connect(endpoint));
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
        const recvTask = drainRecvSocket(server, (received) => {
            const header = decodeMetricHeader(received.parts[0].data());
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
                if (!trySocketSend(client, payload)) {
                    break;
                }
                seq += 1n;
            }
            drainRecvNow(server, (received) => {
                const header = decodeMetricHeader(received.parts[0].data());
                collector.record(header, currentEpochNs());
            });
            if ((Number(seq) & 0x03) === 0) {
                await sleepImmediate();
            }
        }
        for (let i = 0; i < 4; i += 1) {
            drainRecvNow(server, (received) => {
                const header = decodeMetricHeader(received.parts[0].data());
                collector.record(header, currentEpochNs());
            });
            await sleepImmediate();
        }
        stop = true;
        await recvTask;
        const result = await collector.finish();
        return result.latenciesNs;
    }
    finally {
        client.close();
        server.close();
        ctx.close();
    }
}
module.exports = { runDealerDealerBenchmark };
