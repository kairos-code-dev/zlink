// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, sleepMillis, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { configureTlsClient, configureTlsServer } = require('../common/perf_tls');
const { benchmarkEndpoint, parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLIN, POLLCOMPLETION, POLLOUT, applyAutoHwmMsgUnit, applySocketPolicy, applyContextPolicy, applySpotNodeAdmission, createSocketEventWaiter, emitMultiSocketHwmDetail, pollEvents, publishControlUntilSent, subscribeNoWait, trySocketPublish, waitForSpotNodeConnectedPeerCount, waitForRunnerControlConnected, waitForRunnerStart } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.from(Buffer.from('PERF_SPOT_REQREP_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.from(Buffer.from('PERF_SPOT_REQREP_SPOT', 'ascii'));
const TRACE = process.env.PERF_MULTI_SPOT_REQREP_TRACE === '1';
function trace(message) {
    if (TRACE) {
        console.error(`[multi-spot-reqrep-client] ${message}`);
    }
}
function traceValue(label, value) {
    trace(`${label}=${JSON.stringify(value, (_key, item) => typeof item === 'bigint' ? item.toString() : item)}`);
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
function pollCompletionUntil(poller, pollBuffer, stopNs) {
    const remainingNs = stopNs - currentEpochNs();
    if (remainingNs <= 0n) {
        return 0;
    }
    const remainingMs = Number((BigInt(remainingNs) + 999999n) / 1000000n);
    return poller.wait(pollBuffer, Math.min(50, Math.max(1, remainingMs)));
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
function tryRequestSpotReply(spot, payload, timeoutMs, onReply, onDone) {
    try {
        return spot.requestToSpot(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID)
            .message(payload)
            .flags(zlink.SendFlags.DontWait)
            .timeout(timeoutMs)
            .submit((result, parts) => {
            try {
                if (result === zlink.RequestResult.Ok) {
                    onReply(decodeMetricHeaderFromParts(parts));
                }
                else {
                    trace(`request callback result=${result}`);
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
            trace('request submit backpressured');
            return false;
        }
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') ||
            /Resource temporarily unavailable|temporarily unavailable|would block/i.test(text)) {
            trace(`request submit temporarily unavailable: ${text}`);
            return false;
        }
        throw error;
    }
}
function receiveControlStart(controlSub, msgSize) {
    const received = subscribeNoWait(controlSub);
    if (!received) {
        return false;
    }
    try {
        return received.parts[0].data().toString('utf8') === `START,${msgSize}`;
    }
    finally {
        received.close();
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = zlink.createContext();
    applyContextPolicy(ctx, 'client', 'MULTI_SPOT_REQREP');
    const controlPub = zlink.createPubSocket(ctx);
    const controlSub = zlink.createSubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    const node = zlink.createSpotNode(ctx);
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
        const dataRouterEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-reqrep-client-router-${process.pid}`);
        configureTlsServer(node, options.transport);
        configureTlsClient(node, options.transport);
        node.setRoutingId(zlink.RoutingId.from(Buffer.from('PERF_SPOT_REQREP_CLIENT_NODE', 'ascii')));
        node.setRouterBind(dataRouterEndpoint);
        node.setPubBind(dataEndpoint);
        node.connectPeer(options.peerEndpoint);
        for (let i = 0; i < options.clients; i += 1) {
            const spot = node.createSpot();
            spot.setRoutingId(zlink.RoutingId.from(Buffer.from(`PERF_SPOT_REQREP_CLIENT_SPOT_${i}`, 'ascii')));
            slots.push({
                spot,
                payload: createPayload(options.msgSize),
                inflight: false
            });
        }
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
        await sleepMillis(resolveMultiSpotReadySettleMs());
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
        await sleepMillis(resolveMultiSpotControlSettleMs());
        await waitForSpotNodeConnectedPeerCount(node, 1);
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
        await sleepMillis(resolveMultiSpotControlSettleMs());
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
        let runnerStarted = false;
        let controlStarted = false;
        const startFromRunner = waitForRunnerStart(options.msgSize).then(() => {
            runnerStarted = true;
        });
        let nextReadyAt = Date.now();
        while (!runnerStarted || !controlStarted) {
            if (Date.now() >= nextReadyAt) {
                trySocketPublish(controlPub, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
                trySocketPublish(controlPub, CONTROL_TOPIC, 'CONNECTED');
                trySocketPublish(controlPub, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${slots.length}`);
                nextReadyAt = Date.now() + 250;
            }
            controlStarted = controlStarted || receiveControlStart(controlSub, options.msgSize);
            await Promise.race([
                startFromRunner,
                sleepMillis(Math.min(50, Math.max(1, nextReadyAt - Date.now())))
            ]);
        }
        trace('start-ready');
        traceValue('status', node.status());
        traceValue('peers', node.peers());
        traceValue('subjects', node.subjects());
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
        const poller = zlink.createPoller();
        const pollBuffer = zlink.createPollEvents(Math.max(1, slots.length));
        for (let i = 0; i < slots.length; i += 1) {
            poller.add(slots[i].spot, pollEvents(POLLCOMPLETION), i);
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
                    pollCompletionUntil(poller, pollBuffer, activeStopNs);
                    await sleepImmediate();
                }
            }
            const drainStopNs = currentEpochNs() + BigInt(Math.max(1000, requestTimeoutMs * 4)) * 1000000n;
            while (activeSlots.some((slot) => slot.inflight) && currentEpochNs() < drainStopNs) {
                pollCompletionUntil(poller, pollBuffer, drainStopNs);
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
