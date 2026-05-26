// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { createPayload, createRunId, sleepMillis, stampPayload } = require('../common/perf_metrics');
const { benchmarkEndpoint, parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, applySpotNodeAdmission, createSocketEventWaiter, emitMultiSocketHwmDetail, pollEvents, publishControlUntilSent, subscribeNoWait, trySocketPublish } = require('./perf_multi_runtime');
const TOPIC = 'bench';
const CONTROL_TOPIC = 'bench';
// C parity: bindings/c/perf/multi/src/perf_multi_spot_server.cpp
// resolve_spot_latency_only_mode (~197-208). The runner's clean-latency
// second pass sets PERF_MULTI_SPOT_LATENCY_ONLY=1 so the publisher paces
// itself at PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US (default 1000us)
// between successful publishes, leaving the receiver unsaturated so the
// reported latency reflects clean one-message RTT rather than the
// backpressured active-pass tail.
function resolveSpotLatencyOnlyMode() {
    const value = process.env.PERF_MULTI_SPOT_LATENCY_ONLY;
    return value !== undefined && value !== '' && value !== '0';
}
function resolveSpotLatencyOnlyIntervalNs() {
    const raw = Number(process.env.PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US);
    const us = Number.isFinite(raw) && raw >= 1 ? Math.trunc(raw) : 1000;
    return BigInt(us) * 1000n;
}
function trySpotPublish(spot, _channelName, topic, payload, flags) {
    try {
        return spot.publishFrom(topic, payload, flags);
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
function closeQuietly(resource) {
    try {
        resource?.close();
    }
    catch (err) {
        console.error(`[multi-spot-server] close failed: ${err}`);
    }
}
function connectDataEndpoint(node, connectedDataEndpoints, endpoint) {
    if (!endpoint || connectedDataEndpoints.has(endpoint)) {
        return;
    }
    try {
        node.connectPeer(endpoint);
    }
    catch (error) {
        const text = String(error && error.message ? error.message : error);
        if (!/Device or resource busy|resource busy|already/i.test(text)) {
            throw error;
        }
    }
    connectedDataEndpoints.add(endpoint);
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'server', 'MULTI_SPOT');
    const node = new zlink.SpotNode(ctx);
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    let spot = null;
    const payload = createPayload(options.msgSize);
    let readyCount = 0;
    let startRequested = false;
    let stopRequested = false;
    let connectedControlEndpoint = '';
    const connectedDataEndpoints = new Set();
    let controlPoller = null;
    let controlEvents = null;
    let rl = null;
    try {
        configureTlsServer(node, options.transport);
        const dataBindEndpoint = options.peerEndpoint ||
            await benchmarkEndpoint(options.transport, 'multi-spot-data');
        applySpotNodeAdmission(node);
        node.setPubBind(dataBindEndpoint);
        spot = node.createSpot();
        const spotPoller = new zlink.Poller();
        const spotEvents = new zlink.PollEvents(1);
        spotPoller.add(spot, [POLLOUT], 0);
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        applyAutoHwmMsgUnit(ctx, options.msgSize);
        controlPub.bind(options.controlEndpoint);
        controlSub.setSubscription(CONTROL_TOPIC);
        controlPoller = new zlink.Poller();
        controlEvents = new zlink.PollEvents(1);
        controlPoller.add(controlSub, pollEvents(POLLIN), 0);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);
        console.log(`READY,${options.endpoint}`);
        console.log(`CONTROL_READY,${options.controlEndpoint}`);
        rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
        (async () => {
            for await (const line of rl) {
                if (line === `START,${options.msgSize}`) {
                    startRequested = true;
                }
                else if (line.startsWith('CONNECT_CONTROL,')) {
                    const clientEndpoint = line.slice('CONNECT_CONTROL,'.length).trim();
                    if (!clientEndpoint || clientEndpoint === connectedControlEndpoint) {
                        continue;
                    }
                    controlSub.connect(clientEndpoint);
                    connectedControlEndpoint = clientEndpoint;
                    console.log(`CONTROL_CONNECTED,${clientEndpoint}`);
                }
                else if (line.startsWith('DATA_ENDPOINT,')) {
                    const endpoint = line.slice('DATA_ENDPOINT,'.length).trim();
                    connectDataEndpoint(node, connectedDataEndpoints, endpoint);
                }
                else if (line === 'STOP' || line === 'QUIT') {
                    stopRequested = true;
                    break;
                }
            }
        })();
        while (!stopRequested && !(startRequested && readyCount >= options.clients)) {
            let drained = false;
            while (true) {
                const received = subscribeNoWait(controlSub);
                if (!received) {
                    break;
                }
                try {
                    drained = true;
                    const payloadText = received.parts[0].data().toString('utf8');
                    if (payloadText === 'CONNECTED') {
                        continue;
                    }
                    if (payloadText.startsWith('DATA_ENDPOINT,')) {
                        const endpoint = payloadText.slice('DATA_ENDPOINT,'.length).trim();
                        connectDataEndpoint(node, connectedDataEndpoints, endpoint);
                        continue;
                    }
                    if (payloadText.startsWith(`READY_COUNT,${options.msgSize},`)) {
                        readyCount = Number(payloadText.split(',')[2]);
                    }
                }
                finally {
                    received.close();
                }
            }
            if (!(startRequested && readyCount >= options.clients) && !drained) {
                controlPoller.wait(controlEvents, 50);
            }
            await sleepMillis(0);
        }
        if (stopRequested) {
            return;
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);
        const runId = createRunId(1);
        const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
        // C parity: bindings/c/perf/multi/src/perf_multi_spot_server.cpp
        // run_phase / try_publish_locked. Each active iteration stamps one
        // payload, tries DONTWAIT first, then falls back to a blocking send
        // attempt. If both hit transient backpressure, wait for POLLOUT.
        // The control START is a slow joiner, so re-publish it for the first
        // ~250ms so the client SUB sees it once its subscription propagates.
        // C parity: perf_multi_spot_server.cpp run_phase (~334-373) — in
        // latency-only (clean) mode the publisher paces itself, waiting
        // probe_interval after each successful publish so the subscriber
        // never backs up and the measured latency is clean.
        const latencyOnly = resolveSpotLatencyOnlyMode();
        const latencyOnlyIntervalNs = resolveSpotLatencyOnlyIntervalNs();
        let seq = 1n;
        let nextStartAt = Date.now();
        const startRepublishUntil = Date.now() + 250;
        let nextPublishAtNs = process.hrtime.bigint();
        try {
            while (!stopRequested && process.hrtime.bigint() < activeStopNs) {
                if (Date.now() < startRepublishUntil && Date.now() >= nextStartAt) {
                    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `START,${options.msgSize}`);
                    nextStartAt = Date.now() + 50;
                }
                if (latencyOnly) {
                    const nowNs = process.hrtime.bigint();
                    if (nowNs < nextPublishAtNs) {
                        const remainingMs = Number((nextPublishAtNs - nowNs) / 1000000n);
                        await sleepMillis(Math.max(1, Math.min(50, remainingMs)));
                        continue;
                    }
                }
                stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
                if (trySpotPublish(spot, '', TOPIC, payload, zlink.SendFlags.DontWait) ||
                    trySpotPublish(spot, '', TOPIC, payload, zlink.SendFlags.None)) {
                    seq += 1n;
                    if (latencyOnly) {
                        nextPublishAtNs = process.hrtime.bigint() + latencyOnlyIntervalNs;
                    }
                }
                else {
                    spotPoller.wait(spotEvents, -1);
                }
            }
        }
        finally {
            spotEvents.close();
            spotPoller.close();
        }
        while (!stopRequested) {
            await sleepMillis(50);
        }
    }
    finally {
        controlEvents?.close();
        controlPoller?.close();
        rl?.close();
        controlPubWaiter.close();
        closeQuietly(spot);
        closeQuietly(controlPub);
        closeQuietly(controlSub);
        closeQuietly(node);
        closeQuietly(ctx);
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
