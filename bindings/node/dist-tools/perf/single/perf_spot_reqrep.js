// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { applySocketPolicy, benchmarkEndpoint, waitForPostReadySettle } = require('./perf_single_common');
function tryRecvRouted(spot) {
    try {
        return spot.recvRouted(zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
function closeMessageParts(parts) {
    for (const part of parts || []) {
        if (part && typeof part.close === 'function') {
            part.close();
        }
    }
}
async function runSpotReqRepBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    const requester = new zlink.RouterSocket(ctx);
    const replierNode = new zlink.SpotNode(ctx);
    const replier = replierNode.createSpot();
    const endpoint = await benchmarkEndpoint(options.transport, `spot-reqrep-${msgSize}`);
    try {
        applySocketPolicy(requester);
        applySocketPolicy(replier);
        replierNode.bind(endpoint);
        requester.connect(endpoint);
        replier.onDispatchEvent(() => {
            while (true) {
                const received = tryRecvRouted(replier);
                if (!received) {
                    return;
                }
                try {
                    received.reply(received.parts.map((part) => part.data()));
                }
                finally {
                    received.close();
                }
            }
        });
        const runId = createRunId(options.runId ?? 1);
        const probe = createPayload(msgSize);
        stampPayload(probe, {
            phase: 0,
            runId,
            msgSize,
            seq: 1n
        });
        const probeReply = await requester.requestToSpot(replierNode.routingId, replier.routingId, probe, 2000);
        closeMessageParts(probeReply);
        await waitForPostReadySettle(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        const collector = createMetricCollector({
            runId,
            msgSize,
            activeStartNs,
            activeStopNs,
            roundTrip: true,
            sampleCap: Number(process.env.PERF_SINGLE_LATENCY_SAMPLE_CAP ?? 200000)
        });
        const payload = createPayload(msgSize);
        let seq = 2n;
        let inflight = 0;
        const onReply = (result, replyParts) => {
            try {
                if (result === zlink.RequestResult.Ok && replyParts.length > 0) {
                    collector.record(decodeMetricHeader(replyParts[0].data()), currentEpochNs());
                }
            }
            finally {
                closeMessageParts(replyParts);
                inflight -= 1;
            }
        };
        while (currentEpochNs() < activeStopNs) {
            let progressed = false;
            while (inflight < 1 && currentEpochNs() < activeStopNs) {
                stampPayload(payload, {
                    phase: 1,
                    runId,
                    msgSize,
                    seq
                });
                const issued = requester.tryRequestToSpot(replierNode.routingId, replier.routingId, Buffer.from(payload), onReply, 2000);
                if (!issued) {
                    break;
                }
                inflight += 1;
                seq += 1n;
                progressed = true;
            }
            if (!progressed) {
                await sleepImmediate();
            }
        }
        const drainDeadline = Date.now() + 2000;
        while (inflight > 0 && Date.now() < drainDeadline) {
            await sleepImmediate();
        }
        const result = await collector.finish();
        return result.latenciesNs;
    }
    finally {
        replier.close();
        replierNode.close();
        requester.close();
        ctx.close();
    }
}
module.exports = { runSpotReqRepBenchmark };
