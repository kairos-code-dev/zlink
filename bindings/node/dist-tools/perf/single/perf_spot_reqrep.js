// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../..');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { applyContextPolicy, applySpotNodeAdmission, applySocketPolicy, benchmarkEndpoint, configureTlsClient, configureTlsServer, parseSingleBinaryArgs, resolveSingleLatencySampleCap, waitForPostReadySettle } = require('./perf_single_common');
const NODE_RID = zlink.RoutingId.fromBytes(Buffer.from('perf-spot-reqrep-node', 'ascii'));
const SPOT_RID = zlink.RoutingId.fromBytes(Buffer.from('perf-spot-reqrep-spot', 'ascii'));
const REQUESTER_RID = zlink.RoutingId.fromBytes(Buffer.from('perf-spot-reqrep-requester', 'ascii'));
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
function closeReceived(received) {
    if (received && typeof received.close === 'function') {
        received.close();
    }
}
function closeParts(parts) {
    for (const part of parts ?? []) {
        if (part && typeof part.close === 'function') {
            part.close();
        }
    }
}
async function requestSpotReply(requester, payload, timeoutMs) {
    const parts = await requester.requestToSpot(NODE_RID, SPOT_RID, payload, timeoutMs);
    try {
        return decodeMetricHeaderFromParts(parts);
    }
    finally {
        closeParts(parts);
    }
}
async function waitForProbeReady(requester, payload, runId, msgSize, seqRef) {
    const readyTimeoutMs = Number(process.env.PERF_CONNECT_READY_TIMEOUT_MS ?? 5000);
    const deadlineNs = currentEpochNs() + BigInt(Math.max(1, readyTimeoutMs)) * 1000000n;
    while (currentEpochNs() < deadlineNs) {
        stampPayload(payload, {
            phase: 0,
            runId,
            msgSize,
            seq: seqRef.current
        });
        seqRef.current += 1n;
        let reply = null;
        try {
            reply = await requestSpotReply(requester, payload, Math.max(1, readyTimeoutMs));
        }
        catch (error) {
            if (currentEpochNs() >= deadlineNs) {
                throw error;
            }
            await sleepImmediate();
            continue;
        }
        if (reply.phase === 0
            && reply.runId === runId
            && reply.msgSize === msgSize) {
            return;
        }
        await sleepImmediate();
    }
    throw new Error('spot reqrep probe-ready timeout');
}
async function runSpotReqRepBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    applyContextPolicy(ctx);
    const requester = new zlink.RouterSocket(ctx);
    const replierNode = new zlink.SpotNode(ctx);
    applySpotNodeAdmission(replierNode, options);
    const replier = replierNode.createSpot();
    const endpoint = await benchmarkEndpoint(options.transport, `spot-reqrep-${msgSize}`);
    let stopResponder = false;
    let responderLoop = null;
    try {
        applySocketPolicy(requester, options);
        configureTlsServer(replierNode, options.transport);
        configureTlsClient(requester, options.transport);
        requester.setRoutingId(REQUESTER_RID);
        replierNode.setRoutingId(NODE_RID);
        replier.setRoutingId(SPOT_RID);
        replierNode.bind(endpoint);
        requester.connect(endpoint);
        responderLoop = (async () => {
            while (!stopResponder) {
                const received = tryRecvRouted(replier);
                if (!received) {
                    await sleepImmediate();
                    continue;
                }
                try {
                    received.reply(received.parts);
                }
                finally {
                    received.close();
                }
            }
        })();
        const runId = createRunId(options.runId ?? 1);
        const payload = createPayload(msgSize);
        const seqRef = { current: 1n };
        await waitForProbeReady(requester, payload, runId, msgSize, seqRef);
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
        while (currentEpochNs() < activeStopNs) {
            stampPayload(payload, {
                phase: 1,
                runId,
                msgSize,
                seq: seqRef.current
            });
            seqRef.current += 1n;
            collector.record(await requestSpotReply(requester, payload, 2000), currentEpochNs());
        }
        stampPayload(payload, {
            phase: 2,
            runId,
            msgSize,
            seq: seqRef.current
        });
        await requestSpotReply(requester, payload, 2000);
        return collector.finish();
    }
    finally {
        stopResponder = true;
        if (responderLoop) {
            await responderLoop;
        }
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
