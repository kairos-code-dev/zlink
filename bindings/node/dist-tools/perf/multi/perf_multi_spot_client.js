// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { configureTlsClient } = require('../common/perf_tls');
const { createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLOUT, POLLIN, applyAutoHwmMsgUnit, applySocketPolicy, applyContextPolicy, applySpotNodeAdmission, createSocketEventWaiter, emitMultiSocketHwmDetail, pollEvents, publishControlUntilSent, trySocketPublish, waitForControlStart, waitForRunnerControlConnected, waitForRunnerStart } = require('./perf_multi_runtime');
const { isStopTokenParts } = require('../perf_stop_token');
const TOPIC = 'bench';
const CONTROL_TOPIC = 'bench';
const TRACE = process.env.PERF_MULTI_SPOT_TRACE === '1';
function trace(message) {
    if (TRACE) {
        console.error(`[multi-spot-client] ${message}`);
    }
}
// C parity / converged root cause #2: the one-way receiver reuses ONE
// message object across the whole burst-drain instead of allocating a
// fresh TopicMessage per message (C uses a single reused zlink_msg_t in
// drain_spot_client_slot). Reusing the object removes millions of
// allocations + GC pauses from the hot recv path.
function trySpotSubscribeInto(spot, received) {
    try {
        return spot.subscribe(received, zlink.RecvFlags.DontWait);
    }
    catch (error) {
        if (error instanceof zlink.RecvError &&
            (error.result === zlink.RecvResult.NoData || error.internalErrno === 2)) {
            return false;
        }
        const text = String(error && error.message ? error.message : error);
        if (/Device or resource busy|resource busy/i.test(text)) {
            return false;
        }
        throw error;
    }
}
function tryControlPublish(pub, payload) {
    return trySocketPublish(pub, CONTROL_TOPIC, Buffer.from(payload));
}
function closeQuietly(resource) {
    try {
        resource?.close();
    }
    catch (err) {
        console.error(`[multi-spot-client] close failed: ${err}`);
    }
}
function connectPeerIfNeeded(node, endpoint) {
    try {
        node.connectPeer(endpoint);
    }
    catch (error) {
        const text = String(error && error.message ? error.message : error);
        if (!/Device or resource busy|resource busy|already/i.test(text)) {
            throw error;
        }
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_SPOT');
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    const slots = [];
    let sharedNode = null;
    let collector = null;
    try {
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        applyAutoHwmMsgUnit(controlPub, options.msgSize);
        applyAutoHwmMsgUnit(controlSub, options.msgSize);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
        controlPub.bind(options.controlEndpoint);
        console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
        controlSub.setSubscription(CONTROL_TOPIC);
        controlSub.connect(options.serverControlEndpoint);
        await waitForRunnerControlConnected();
        trace('control-connected');
        trace(`creating-slots count=${options.clients}`);
        sharedNode = new zlink.SpotNode(ctx);
        configureTlsClient(sharedNode, options.transport);
        applySpotNodeAdmission(sharedNode);
        connectPeerIfNeeded(sharedNode, options.peerEndpoint);
        trace('shared-node connected');
        const spotCount = Math.max(1, Math.trunc(options.clients));
        for (let i = 0; i < spotCount; i += 1) {
            trace(`slot-${i} create-spot`);
            const spot = sharedNode.createSpot();
            spot.setSubscription(TOPIC);
            slots.push({ spot });
        }
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(sharedNode, 'spotnode_data', options.transport, options.msgSize);
        const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
        while (Date.now() < stabilizationDeadline) {
            tryControlPublish(controlPub, 'CONNECTED');
            await sleepImmediate();
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
        const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
        while (Date.now() < controlSettleDeadline) {
            await sleepImmediate();
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${options.clients}`);
        console.log(`CLIENT_READY,${options.msgSize}`);
        trace('client-ready');
        // C parity: bindings/c/perf/multi/src/perf_multi_spot_client.cpp
        // wait_msg_size_start_with_ready_republish (~1270-1306). The control
        // PUB->SUB link is a slow joiner; the single READY_COUNT/CONNECTED
        // publishes above may be dropped before the server's SUB subscription
        // propagates. C does NOT block on a one-shot publish — it loops,
        // re-publishing READY_COUNT every 250ms while draining inbound control
        // until the START command for this size arrives (or the bounded phase
        // timeout elapses). The runner also forwards START on stdin. Wait for
        // EITHER the control-channel START or the stdin START, whichever lands
        // first, while keeping READY_COUNT alive so the server can observe it.
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
                await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${options.clients}`);
                nextReadyAt = Date.now() + 250;
            }
            await Promise.race([
                startFromRunner,
                startFromControl,
                sleepImmediate()
            ]);
        }
        trace('start-handshake-done');
        const controlStartNs = currentEpochNs();
        const activeStopNs = controlStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        collector = createMetricCollector({
            runId: createRunId(1),
            msgSize: options.msgSize,
            activeStartNs: controlStartNs,
            activeStopNs,
        });
        trace('recv-loop-start');
        // C parity: bindings/c/perf/multi/src/perf_multi_spot_client.cpp
        // spot_client_recv_worker_loop / drain_spot_client_slot (~809-851).
        // The phase ends on the server's wire STOP token (PERF_MULTI_TEST
        // _POLICY § 1.3.1), checked BEFORE the metric-header decode (C
        // perf_common.hpp is_stop_token; mirrors the fixed cpp/dotnet path
        // and the node MULTI_PUBSUB client). Each pass DONTWAIT burst-drains
        // every slot; when no slot progressed it does a BOUNDED poller wait
        // plus one event-loop turn (await sleepImmediate). Unlike a plain
        // SUB socket, a SpotNode spot is pumped by the binding's libuv-
        // integrated dispatch, so a pure synchronous `-1` waitMany with no
        // event-loop turn would starve the spot pump and never deliver — the
        // reqrep client uses the same waitMany + sleepImmediate cadence. The
        // bounded poll timeout only caps the idle wait so the active-deadline
        // safety bound stays prompt; the stop token remains the real end
        // signal. An anchored fallback deadline (active duration + size-
        // scaled drain grace, matching C resolve_spot_drain_grace_ns) bounds
        // the loop if the token is lost so the process always terminates.
        const poller = new zlink.Poller();
        const graceMultiplier = options.msgSize >= 262144
            ? 4
            : (options.msgSize >= 131072 ? 2 : 1);
        const fallbackDeadlineNs = controlStartNs
            + BigInt(Math.ceil(options.duration * 1000 * (1 + graceMultiplier)))
                * 1000000n;
        try {
            for (let i = 0; i < slots.length; i += 1) {
                poller.add(slots[i].spot, pollEvents(POLLIN), i);
            }
            const expectedRunId = createRunId(1);
            // Per-pass per-slot burst cap: with 100 backlogged slots a fully
            // unbounded inner drain on slot 0 can starve the rest and never let
            // the outer hard-deadline / event-loop-pump run. C drains a bounded
            // burst per slot then sweeps on (drain_spot_client_slot is invoked
            // per poller wake, not an infinite single-slot loop).
            const burstCap = 4096;
            const reusable = new zlink.TopicMessage();
            let stopReceived = false;
            let deadlineReached = false;
            while (!stopReceived && !deadlineReached) {
                // C parity: wait_spot_sender_window_done bounds the receive phase
                // by an absolute time deadline (sender window end + size-scaled
                // drain grace) regardless of in-flight backlog. Check it every
                // outer pass — not only when idle — so a continuous backlog can
                // never wedge termination.
                if (currentEpochNs() >= fallbackDeadlineNs) {
                    deadlineReached = true;
                    trace('recv-loop fallback-deadline');
                    break;
                }
                let progressed = false;
                for (let i = 0; i < slots.length && !stopReceived; i += 1) {
                    const { spot } = slots[i];
                    let drained = 0;
                    while (drained < burstCap) {
                        if (!trySpotSubscribeInto(spot, reusable)) {
                            break;
                        }
                        drained += 1;
                        progressed = true;
                        if (isStopTokenParts(reusable.parts)) {
                            stopReceived = true;
                            break;
                        }
                        const header = decodeMetricHeaderFromParts(reusable.parts);
                        if (!header
                            || (header.runId >>> 0) !== expectedRunId
                            || (header.msgSize >>> 0) !== options.msgSize) {
                            continue;
                        }
                        collector.record(header, currentEpochNs());
                    }
                }
                if (stopReceived) {
                    break;
                }
                if (!progressed) {
                    poller.waitMany(Math.max(1, poller.size), 20);
                }
                // Always yield an event-loop turn so the SpotNode dispatch pump
                // runs (spots are libuv-integrated; a pure sync loop starves them).
                await sleepImmediate();
            }
        }
        finally {
            poller.close();
        }
        trace('recv-loop-done');
        const result = collector ? await collector.finish() : { latenciesNs: [] };
        for (const metricLine of summarizeMetrics('MULTI_SPOT', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
        trace('result-flushed');
        await new Promise((resolve) => process.stdout.write('', resolve));
        process.exit(0);
    }
    finally {
        controlPubWaiter.close();
        controlSubWaiter.close();
        closeQuietly(controlSub);
        closeQuietly(controlPub);
        for (const slot of slots) {
            closeQuietly(slot.spot);
        }
        closeQuietly(sharedNode);
        closeQuietly(ctx);
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
