// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const trySpotSubscribe = (spot) => {
    try {
        return spot.subscribe(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
};
const trySpotPublish = (spot, topic, payload) => {
    try {
        spot.publish(topic, payload, zlink.SendFlags.DontWait);
        return true;
    }
    catch (error) {
        if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        throw error;
    }
};
async function runSpotBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const node = new zlink.SpotNode(ctx);
    const spot = new zlink.Spot(node);
    const topic = 'perf:spot';
    try {
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
        spot.setSubscription(topic);
        const recvTask = (async () => {
            while (!stop) {
                const received = trySpotSubscribe(spot);
                if (!received) {
                    await sleepImmediate();
                    continue;
                }
                const header = decodeMetricHeader(received.parts[0].data);
                collector.record(header, currentEpochNs());
            }
        })();
        while (process.hrtime.bigint() < stopAtNs) {
            for (let i = 0; i < 256 && process.hrtime.bigint() < stopAtNs; i += 1) {
                stampPayload(payload, {
                    phase: process.hrtime.bigint() < warmupUntilNs ? 0 : 1,
                    runId,
                    msgSize,
                    seq
                });
                if (!trySpotPublish(spot, topic, payload)) {
                    break;
                }
                seq += 1n;
            }
            while (true) {
                const received = trySpotSubscribe(spot);
                if (!received) {
                    break;
                }
                const header = decodeMetricHeader(received.parts[0].data);
                collector.record(header, currentEpochNs());
            }
            if ((Number(seq) & 0x03) === 0) {
                await sleepImmediate();
            }
        }
        for (let i = 0; i < 4; i += 1) {
            while (true) {
                const received = trySpotSubscribe(spot);
                if (!received) {
                    break;
                }
                const header = decodeMetricHeader(received.parts[0].data);
                collector.record(header, currentEpochNs());
            }
            await sleepImmediate();
        }
        stop = true;
        await recvTask;
        const result = await collector.finish();
        return result.latenciesNs;
    }
    finally {
        spot.close();
        node.close();
        ctx.close();
    }
}
module.exports = { runSpotBenchmark };
