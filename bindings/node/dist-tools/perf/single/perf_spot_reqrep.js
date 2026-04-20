// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { applyContextPolicy, applySocketPolicy, benchmarkEndpoint, parseSingleBinaryArgs, resolveSingleLatencySampleCap, waitForPostReadySettle } = require('./perf_single_common');
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
    applyContextPolicy(ctx);
    const requester = new zlink.RouterSocket(ctx);
    const replierNode = new zlink.SpotNode(ctx);
    const replier = replierNode.createSpot();
    const endpoint = await benchmarkEndpoint(options.transport, `spot-reqrep-${msgSize}`);
    try {
        applySocketPolicy(requester, options);
        applySocketPolicy(replier, options);
        replierNode.bind(endpoint);
        requester.connect(endpoint);
        replier.onDispatchEvent(() => {
            while (true) {
                const received = tryRecvRouted(replier);
                if (!received) {
                    return;
                }
                try {
                    received.reply(received.parts);
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
            sampleCap: resolveSingleLatencySampleCap()
        });
        const payload = createPayload(msgSize);
        let seq = 2n;
        while (currentEpochNs() < activeStopNs) {
            stampPayload(payload, {
                phase: 1,
                runId,
                msgSize,
                seq
            });
            const replyParts = await requester.requestToSpot(replierNode.routingId, replier.routingId, payload, 2000);
            try {
                if (replyParts.length > 0) {
                    collector.record(decodeMetricHeaderFromParts(replyParts), currentEpochNs());
                }
            }
            finally {
                closeMessageParts(replyParts);
            }
            seq += 1n;
        }
        return collector.finish();
    }
    finally {
        replier.close();
        replierNode.close();
        requester.close();
        ctx.close();
    }
}
module.exports = { runSpotReqRepBenchmark };
if (require.main === module) {
    (async () => {
        const options = parseSingleBinaryArgs(process.argv.slice(2));
        const result = await runSpotReqRepBenchmark(options.msgSize, options);
        for (const line of summarizeMetrics('SPOT_REQREP', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
