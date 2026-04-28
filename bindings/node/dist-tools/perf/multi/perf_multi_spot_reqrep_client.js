// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { benchmarkEndpoint, parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applySocketPolicy, applyContextPolicy, createSocketEventWaiter, resolveMultiLatencySampleCap, subscribeNoWait, trySocketPublish, waitForConnectionReady } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'perf.control';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii'));
const TRACE = process.env.PERF_MULTI_SPOT_REQREP_TRACE === '1';
function trace(message) {
    if (TRACE) {
        console.error(`[multi-spot-reqrep-client] ${message}`);
    }
}
function closeReceived(received) {
    if (received && typeof received.close === 'function') {
        received.close();
    }
}
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
function closeParts(parts) {
    for (const part of parts ?? []) {
        if (part && typeof part.close === 'function') {
            part.close();
        }
    }
}
function closeQuietly(resource) {
    try {
        resource?.close();
    }
    catch {
    }
}
async function requestSpotReply(router, payload, timeoutMs) {
    const parts = await router.requestToSpot(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID, payload, timeoutMs);
    try {
        return decodeMetricHeaderFromParts(parts);
    }
    finally {
        closeParts(parts);
    }
}
async function waitForProbeReady(routers, payloads, runId, msgSize) {
    const timeoutMs = Number(process.env.PERF_MULTI_SPOT_REQREP_PROBE_TIMEOUT_MS ?? 20000);
    const deadline = Date.now() + Math.max(1, timeoutMs);
    const ready = new Set();
    let seq = 1n;
    while (Date.now() < deadline && ready.size < routers.length) {
        for (let i = 0; i < routers.length; i += 1) {
            if (ready.has(i)) {
                continue;
            }
            stampPayload(payloads[i], { phase: 0, runId, msgSize, seq });
            seq += 1n;
            try {
                const reply = await requestSpotReply(routers[i], payloads[i], 1000);
                if (reply && reply.phase === 0 && reply.runId === runId && reply.msgSize === msgSize) {
                    ready.add(i);
                }
            }
            catch {
            }
        }
        if (ready.size < routers.length) {
            await sleepImmediate();
        }
    }
    if (ready.size < routers.length) {
        throw new Error(`spot reqrep probe readiness timeout ${ready.size}/${routers.length}`);
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_SPOT_REQREP');
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    const routers = [];
    const payloads = [];
    const poller = new zlink.Poller();
    const replierNode = new zlink.SpotNode(ctx);
    const replier = replierNode.createSpot();
    let rl = null;
    let stopResponder = false;
    let responderLoop = null;
    try {
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        controlPub.bind(options.controlEndpoint);
        console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
        controlSub.setSubscription(CONTROL_TOPIC);
        await waitForConnectionReady(controlSub, () => controlSub.connect(options.serverControlEndpoint));
        console.log(`CONTROL_CONNECTED,${options.serverControlEndpoint}`);
        trace('control-connected');
        applySocketPolicy(replier);
        replierNode.setRoutingId(SERVER_NODE_ROUTING_ID);
        replier.setRoutingId(SERVER_SPOT_ROUTING_ID);
        const dataEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-reqrep-client-${process.pid}`);
        replierNode.bind(dataEndpoint);
        responderLoop = (async () => {
            while (!stopResponder) {
                const received = tryRecvRouted(replier);
                if (!received) {
                    await sleepImmediate();
                    continue;
                }
                try {
                    received.reply(received.parts.map((part) => zlink.Message.from(Buffer.from(part.data()))));
                }
                finally {
                    closeReceived(received);
                }
            }
        })();
        for (let i = 0; i < options.clients; i += 1) {
            const router = new zlink.RouterSocket(ctx);
            applySocketPolicy(router);
            router.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`PERF_SPOT_REQREP_CLIENT_${i}`, 'ascii')));
            routers.push(router);
            payloads.push(createPayload(options.msgSize));
        }
        for (let i = 0; i < routers.length; i += 1) {
            routers[i].connect(dataEndpoint);
        }
        const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
        while (Date.now() < stabilizationDeadline) {
            await sleepImmediate();
        }
        await waitForProbeReady(routers, payloads, createRunId(1), options.msgSize);
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from('CONNECTED'))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
        while (Date.now() < controlSettleDeadline) {
            await sleepImmediate();
        }
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`READY_COUNT,${options.msgSize},${routers.length}`))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        console.log(`CLIENT_READY,${options.msgSize}`);
        trace('client-ready');
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        let startRequested = false;
        (async () => {
            for await (const line of rl) {
                if (line === `START,${options.msgSize}`) {
                    startRequested = true;
                }
            }
        })();
        while (!startRequested) {
            let drained = false;
            while (true) {
                const received = subscribeNoWait(controlSub);
                if (!received) {
                    break;
                }
                drained = true;
            }
            if (!startRequested && !drained) {
                await sleepImmediate();
            }
        }
        trace('start-ready');
        const runId = createRunId(1);
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        const collector = createMetricCollector({
            runId,
            msgSize: options.msgSize,
            activeStartNs,
            activeStopNs,
            roundTrip: true,
            sampleCap: resolveMultiLatencySampleCap()
        });
        let seq = 1n;
        while (currentEpochNs() < activeStopNs) {
            for (let i = 0; i < routers.length; i += 1) {
                stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                collector.record(await requestSpotReply(routers[i], payloads[i], 2000), currentEpochNs());
                seq += 1n;
            }
        }
        trace('requests-complete');
        const result = await collector.finish();
        for (const metricLine of summarizeMetrics('MULTI_SPOT_REQREP', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
        trace('result-flushed');
    }
    finally {
        stopResponder = true;
        if (responderLoop) {
            await responderLoop;
        }
        rl?.close();
        closeQuietly(poller);
        controlSubWaiter.close();
        controlPubWaiter.close();
        closeQuietly(controlSub);
        closeQuietly(controlPub);
        for (const router of routers) {
            closeQuietly(router);
        }
        closeQuietly(replier);
        closeQuietly(replierNode);
        closeQuietly(ctx);
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
