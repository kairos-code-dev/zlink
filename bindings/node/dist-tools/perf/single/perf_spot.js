// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const TOPIC = 'perf.topic';
const DEBUG = process.env.PERF_DEBUG === '1';
const POLLIN = 1;
function trySpotSubscribe(spot) {
    try {
        return spot.subscribe(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError
            && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
function trySpotPublish(spot, topic, payload) {
    try {
        spot.publish(topic, payload, zlink.SendFlags.DontWait);
        return true;
    }
    catch (error) {
        if (error instanceof zlink.SubmitError
            && error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        throw error;
    }
}
async function runSpotBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const pubNode = new zlink.SpotNode(ctx);
    const subNode = new zlink.SpotNode(ctx);
    const pubSpot = pubNode.createSpot();
    const subSpot = subNode.createSpot();
    const poller = new zlink.Poller();
    const endpoint = `inproc://perf-spot-${process.pid}-${msgSize}`;
    try {
        if (DEBUG) {
            console.error('spot setup start');
        }
        pubNode.bind(endpoint);
        if (DEBUG) {
            console.error('spot pub bound');
        }
        subNode.connectPeer(endpoint);
        if (DEBUG) {
            console.error('spot sub connected');
        }
        subSpot.setSubscription(TOPIC);
        if (DEBUG) {
            console.error('spot subscription set');
        }
        poller.addSocket(subSpot, POLLIN);
        await new Promise((resolve) => setTimeout(resolve, 100));
        const startedAtNs = process.hrtime.bigint();
        const runId = createRunId();
        const collector = createMetricCollector({ runId, msgSize });
        const payload = createPayload(msgSize);
        let seq = 1n;
        let ready = false;
        const readyDeadline = Date.now() + 10_000;
        while (!ready && Date.now() < readyDeadline) {
            stampPayload(payload, {
                phase: 0,
                runId,
                msgSize,
                seq
            });
            if (trySpotPublish(pubSpot, TOPIC, payload)) {
                seq += 1n;
            }
            while (true) {
                const received = trySpotSubscribe(subSpot);
                if (!received) {
                    break;
                }
                const header = decodeMetricHeader(received.parts[0].data());
                if (header) {
                    ready = true;
                    collector.record(header, currentEpochNs());
                }
            }
            await sleepImmediate();
        }
        if (DEBUG) {
            console.error(`spot ready=${ready} seq=${seq.toString()}`);
        }
        const warmupUntilNs = startedAtNs + BigInt(Math.floor(options.warmup * 1_000_000_000));
        const stopAtNs = startedAtNs
            + BigInt(Math.floor((options.warmup + options.duration) * 1_000_000_000));
        while (process.hrtime.bigint() < stopAtNs) {
            for (let i = 0; i < 256 && process.hrtime.bigint() < stopAtNs; i += 1) {
                stampPayload(payload, {
                    phase: process.hrtime.bigint() < warmupUntilNs ? 0 : 1,
                    runId,
                    msgSize,
                    seq
                });
                if (!trySpotPublish(pubSpot, TOPIC, payload)) {
                    break;
                }
                seq += 1n;
            }
            let readySockets = [];
            try {
                readySockets = poller.poll(50);
            }
            catch (error) {
                const text = String(error && error.message ? error.message : error);
                if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
                    await sleepImmediate();
                    continue;
                }
                throw error;
            }
            if (readySockets.length !== 0) {
                while (true) {
                    const received = trySpotSubscribe(subSpot);
                    if (!received) {
                        break;
                    }
                    const header = decodeMetricHeader(received.parts[0].data());
                    collector.record(header, currentEpochNs());
                }
            }
            if ((Number(seq) & 0x03) === 0) {
                await sleepImmediate();
            }
        }
        for (let i = 0; i < 4; i += 1) {
            while (true) {
                const received = trySpotSubscribe(subSpot);
                if (!received) {
                    break;
                }
                const header = decodeMetricHeader(received.parts[0].data());
                collector.record(header, currentEpochNs());
            }
            await sleepImmediate();
        }
        const result = await collector.finish();
        if (DEBUG) {
            console.error(`spot accepted=${result.accepted} rejected=${result.rejected}`);
        }
        return result.latenciesNs;
    }
    finally {
        poller.close();
        pubSpot.close();
        subSpot.close();
        pubNode.close();
        subNode.close();
        ctx.close();
    }
}
module.exports = { runSpotBenchmark };
