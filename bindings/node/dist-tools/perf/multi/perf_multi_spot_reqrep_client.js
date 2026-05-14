// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { configureTlsClient, configureTlsServer } = require('../common/perf_tls');
const { benchmarkEndpoint, parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applySocketPolicy, applyContextPolicy, applySpotNodeAdmission, createCallbackEventWaiter, createSocketEventWaiter, emitMultiSocketHwmDetail, publishControlUntilSent, waitForControlStart, waitForRunnerControlConnected, waitForRunnerStart } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii'));
const TRACE = process.env.PERF_MULTI_SPOT_REQREP_TRACE === '1';
function trace(message) {
    if (TRACE) {
        console.error(`[multi-spot-reqrep-client] ${message}`);
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
    catch (err) {
        console.error(`[multi-spot-reqrep-client] close failed: ${err}`);
    }
}
async function requestSpotReply(spot, payload, timeoutMs) {
    const parts = await spot.requestToSpot(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID)
        .message(payload)
        .timeout(timeoutMs)
        .submitAsync();
    try {
        return decodeMetricHeaderFromParts(parts);
    }
    finally {
        closeParts(parts);
    }
}
function tryRequestSpotReply(spot, payload, timeoutMs, onReply, onDone) {
    try {
        return spot.requestToSpot(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID)
            .message(payload)
            .timeout(timeoutMs)
            .flags(zlink.SendFlags.DontWait)
            .submit((result, parts) => {
            try {
                if (result === zlink.RequestResult.Ok) {
                    onReply(decodeMetricHeaderFromParts(parts));
                }
            }
            finally {
                closeParts(parts);
                onDone();
            }
        });
    }
    catch (error) {
        if (error instanceof zlink.SubmitError &&
            error.result === zlink.SubmitResult.Backpressured) {
            return false;
        }
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') ||
            /Resource temporarily unavailable|temporarily unavailable|would block/i.test(text)) {
            return false;
        }
        throw error;
    }
}
async function waitForProbeReady(slots, runId, msgSize) {
    const timeoutMs = Number(process.env.PERF_MULTI_SPOT_REQREP_PROBE_TIMEOUT_MS ?? 20000);
    const deadline = Date.now() + Math.max(1, timeoutMs);
    const ready = new Set();
    let seq = 1n;
    while (Date.now() < deadline && ready.size < slots.length) {
        for (let i = 0; i < slots.length; i += 1) {
            if (ready.has(i)) {
                continue;
            }
            stampPayload(slots[i].payload, { phase: 0, runId, msgSize, seq });
            seq += 1n;
            try {
                const reply = await requestSpotReply(slots[i].spot, slots[i].payload, 1000);
                if (reply && reply.phase === 0 && reply.runId === runId && reply.msgSize === msgSize) {
                    ready.add(i);
                }
            }
            catch (err) {
                trace(`probe failed: ${err}`);
            }
        }
        if (ready.size < slots.length) {
            await sleepImmediate();
        }
    }
    if (ready.size < slots.length) {
        throw new Error(`spot reqrep probe readiness timeout ${ready.size}/${slots.length}`);
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
    const node = new zlink.SpotNode(ctx);
    const slots = [];
    try {
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        applyAutoHwmMsgUnit(controlPub, options.msgSize);
        applyAutoHwmMsgUnit(controlSub, options.msgSize);
        applySpotNodeAdmission(node);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
        controlPub.bind(options.controlEndpoint);
        console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
        controlSub.setSubscription(CONTROL_TOPIC);
        controlSub.connect(options.serverControlEndpoint);
        await waitForRunnerControlConnected();
        trace('control-connected');
        const dataEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-reqrep-client-${process.pid}`);
        configureTlsServer(node, options.transport);
        configureTlsClient(node, options.transport);
        node.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_CLIENT_NODE', 'ascii')));
        node.bind(dataEndpoint);
        node.connectPeer(options.peerEndpoint);
        for (let i = 0; i < options.clients; i += 1) {
            const spot = node.createSpot();
            spot.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`PERF_SPOT_REQREP_CLIENT_SPOT_${i}`, 'ascii')));
            slots.push({
                spot,
                payload: createPayload(options.msgSize),
                inflight: false
            });
        }
        const sendReady = createCallbackEventWaiter((handler) => {
            for (const slot of slots) {
                slot.spot.onSendReady(handler);
            }
        });
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
        const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
        while (Date.now() < stabilizationDeadline) {
            await sleepImmediate();
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
        await waitForProbeReady(slots, createRunId(1), options.msgSize);
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
        const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
        while (Date.now() < controlSettleDeadline) {
            await sleepImmediate();
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${slots.length}`);
        console.log(`CLIENT_READY,${options.msgSize}`);
        trace('client-ready');
        await waitForRunnerStart(options.msgSize);
        await waitForControlStart(controlSub, controlSubWaiter, options.msgSize);
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
        });
        let seq = 1n;
        while (currentEpochNs() < activeStopNs) {
            let progressed = false;
            for (const slot of slots) {
                if (slot.inflight) {
                    continue;
                }
                stampPayload(slot.payload, { phase: 1, runId, msgSize: options.msgSize, seq });
                const submitted = tryRequestSpotReply(slot.spot, slot.payload, 2000, (header) => {
                    collector.record(header, currentEpochNs());
                }, () => {
                    slot.inflight = false;
                });
                if (!submitted) {
                    continue;
                }
                slot.inflight = true;
                seq += 1n;
                progressed = true;
            }
            if (!progressed) {
                if (slots.some((slot) => !slot.inflight)) {
                    await sendReady.wait();
                }
                else {
                    await sleepImmediate();
                }
            }
        }
        while (slots.some((slot) => slot.inflight)) {
            await sleepImmediate();
        }
        trace('requests-complete');
        const result = await collector.finish();
        for (const metricLine of summarizeMetrics('MULTI_SPOT_REQREP', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
        trace('result-flushed');
    }
    finally {
        controlPubWaiter.close();
        controlSubWaiter.close();
        closeQuietly(controlSub);
        closeQuietly(controlPub);
        for (const slot of slots) {
            closeQuietly(slot.spot);
        }
        closeQuietly(node);
        closeQuietly(ctx);
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
