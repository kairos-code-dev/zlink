// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { configureTlsClient, configureTlsServer } = require('../common/perf_tls');
const { benchmarkEndpoint, parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applySocketPolicy, applyContextPolicy, applySpotNodeAdmission, createSocketEventWaiter, emitMultiSocketHwmDetail, publishControlUntilSent, waitForControlStart, waitForRunnerControlConnected, waitForRunnerStart } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii'));
const TRACE = process.env.PERF_MULTI_SPOT_REQREP_TRACE === '1';
function trace(message) {
    if (TRACE) {
        console.error(`[multi-spot-reqrep-client] ${message}`);
    }
}
function activeSpotSlotLimit(totalSlots, msgSize) {
    if (msgSize >= 131072) {
        return Math.min(totalSlots, 8);
    }
    if (msgSize >= 65536) {
        return Math.min(totalSlots, 32);
    }
    return totalSlots;
}
function activeRequestTimeoutMs() {
    const raw = Number(process.env.PERF_MULTI_RCVTIMEO_MS ?? process.env.PERF_MULTI_SNDTIMEO_MS);
    return Number.isFinite(raw) && raw > 0 ? Math.trunc(raw) : 200;
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
    const parts = await spot.requestToSpotFrom(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID, payload, timeoutMs);
    try {
        return decodeMetricHeaderFromParts(parts);
    }
    finally {
        closeParts(parts);
    }
}
function tryRequestSpotReply(spot, payload, timeoutMs, onReply, onDone) {
    try {
        return spot.requestToSpotFrom(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID, payload, (result, parts) => {
            try {
                if (result === zlink.RequestResult.Ok) {
                    onReply(decodeMetricHeaderFromParts(parts));
                }
            }
            finally {
                closeParts(parts);
                onDone();
            }
        }, zlink.SendFlags.DontWait, timeoutMs);
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
        applyAutoHwmMsgUnit(ctx, options.msgSize);
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
        // C parity: bindings/c/perf/multi/src/perf_multi_spot_client.cpp
        // wait_msg_size_start_with_ready_republish (~1270-1306). The control
        // PUB->SUB link is a slow joiner; a one-shot READY_COUNT can be
        // dropped before the server SUB subscription propagates (publish is
        // now bounded, not an infinite block). Re-publish READY_COUNT every
        // 250ms while waiting for START (stdin from the runner or control
        // channel, whichever lands first) so the server always observes the
        // ready count and the handshake never wedges.
        let started = false;
        const startFromRunner = waitForRunnerStart(options.msgSize).then(() => {
            started = true;
        });
        const startFromControl = waitForControlStart(controlSub, controlSubWaiter, options.msgSize).then(() => {
            started = true;
        });
        let nextReadyAt = Date.now();
        while (!started) {
            if (Date.now() >= nextReadyAt) {
                await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${slots.length}`);
                nextReadyAt = Date.now() + 250;
            }
            await Promise.race([startFromRunner, startFromControl, sleepImmediate()]);
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
        });
        let seq = 1n;
        const poller = new zlink.Poller();
        const pollBuffer = new zlink.PollEvents(Math.max(1, slots.length));
        for (let i = 0; i < slots.length; i += 1) {
            poller.add(slots[i].spot, [POLLIN], i);
        }
        const activeSlots = slots.slice(0, activeSpotSlotLimit(slots.length, options.msgSize));
        const requestTimeoutMs = activeRequestTimeoutMs();
        try {
            while (currentEpochNs() < activeStopNs) {
                let progressed = false;
                for (const slot of activeSlots) {
                    if (slot.inflight) {
                        continue;
                    }
                    stampPayload(slot.payload, { phase: 1, runId, msgSize: options.msgSize, seq });
                    const submitted = tryRequestSpotReply(slot.spot, slot.payload, requestTimeoutMs, (header) => {
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
                    poller.wait(pollBuffer, 50);
                    await sleepImmediate();
                }
            }
            while (activeSlots.some((slot) => slot.inflight)) {
                poller.wait(pollBuffer, 50);
                await sleepImmediate();
            }
        }
        finally {
            pollBuffer.close();
            poller.close();
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
