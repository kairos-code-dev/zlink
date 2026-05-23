// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { requireNative } = require('../../dist/zlink/runtime/native/native');
const { createPayload, createRunId, sleepImmediate, summarizeMetrics, } = require('../common/perf_metrics');
const { configureTlsClient, configureTlsServer } = require('../common/perf_tls');
const { benchmarkEndpoint, parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, applySpotNodeAdmission, createSocketEventWaiter, emitMultiSocketHwmDetail, publishControlUntilSent, waitForControlStart, waitForRunnerControlConnected, waitForRunnerStart } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_SENDSEND_NODE', 'ascii'));
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_SENDSEND_SPOT', 'ascii'));
function activeSpotSlotLimit(totalSlots, msgSize) {
    if (msgSize >= 131072) {
        return Math.min(totalSlots, 8);
    }
    if (msgSize >= 65536) {
        return Math.min(totalSlots, 32);
    }
    return totalSlots;
}
function closeQuietly(resource) {
    try {
        resource?.close();
    }
    catch (err) {
        console.error(`[multi-spot-sendsend-client] close failed: ${err}`);
    }
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_SPOT_SENDSEND');
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
        controlPub.bind(options.controlEndpoint);
        console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
        controlSub.setSubscription(CONTROL_TOPIC);
        controlSub.connect(options.serverControlEndpoint);
        await waitForRunnerControlConnected();
        node.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_SENDSEND_CLIENT_NODE', 'ascii')));
        const dataEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-sendsend-client-${process.pid}`);
        const dataRouterEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-sendsend-client-router-${process.pid}`);
        configureTlsServer(node, options.transport);
        configureTlsClient(node, options.transport);
        node.setRouterBind(dataRouterEndpoint);
        node.setPubBind(dataEndpoint);
        node.connectPeer(options.peerEndpoint);
        for (let i = 0; i < options.clients; i += 1) {
            const spot = node.createSpot();
            spot.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`PERF_SPOT_SENDSEND_CLIENT_SPOT_${i}`, 'ascii')));
            slots.push({
                spot,
                payload: createPayload(options.msgSize)
            });
        }
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
        const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
        while (Date.now() < stabilizationDeadline) {
            await sleepImmediate();
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
        const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
        while (Date.now() < controlSettleDeadline) {
            await sleepImmediate();
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${slots.length}`);
        console.log(`CLIENT_READY,${options.msgSize}`);
        // C parity: wait_msg_size_start_with_ready_republish — re-publish
        // READY_COUNT every 250ms while waiting for START (stdin or control)
        // so the slow-joiner control link can never wedge the handshake now
        // that publishControlUntilSent is bounded (returns on timeout).
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
        const runId = createRunId(1);
        const activeSlots = slots.slice(0, activeSpotSlotLimit(slots.length, options.msgSize));
        const result = requireNative().spotPerfSendSendLoop(activeSlots.map((slot) => slot.spot.nativeHandle()), SERVER_NODE_ROUTING_ID.toBytes(), SERVER_SPOT_ROUTING_ID.toBytes(), activeSlots.map((slot) => slot.payload), options.msgSize, options.duration, runId);
        for (const metricLine of summarizeMetrics('MULTI_SPOT_SENDSEND', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
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
