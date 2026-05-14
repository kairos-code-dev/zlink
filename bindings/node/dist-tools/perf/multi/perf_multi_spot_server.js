// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsServer } = require('../common/perf_tls');
const { createPayload, createRunId, sleepImmediate, stampPayload } = require('../common/perf_metrics');
const { benchmarkEndpoint, parseMultiArgs } = require('./perf_multi_common');
const { POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, applySpotNodeAdmission, createSocketEventWaiter, emitMultiSocketHwmDetail, emitMultiSpotNodeHwmSnapshot, subscribeNoWait, trySocketPublish } = require('./perf_multi_runtime');
const TOPIC = 'bench';
const CONTROL_TOPIC = 'bench';
function integerEnv(name, fallback) {
    const raw = process.env[name];
    if (raw === undefined || raw === '') {
        return fallback;
    }
    const parsed = Number(raw);
    return Number.isFinite(parsed) ? Math.trunc(parsed) : fallback;
}
function latencyOnlyEnabled() {
    const raw = process.env.PERF_MULTI_SPOT_LATENCY_ONLY;
    return raw !== undefined && raw !== '' && raw !== '0';
}
async function sleepMicroseconds(microseconds) {
    const milliseconds = Math.max(1, Math.ceil(microseconds / 1000));
    await new Promise((resolve) => setTimeout(resolve, milliseconds));
}
function trySpotPublish(spot, _channelName, topic, payload) {
    try {
        return spot.publish(topic)
            .message(payload)
            .flags(zlink.SendFlags.DontWait)
            .submit();
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
    let rl = null;
    try {
        configureTlsServer(node, options.transport);
        const dataBindEndpoint = options.peerEndpoint ||
            await benchmarkEndpoint(options.transport, 'multi-spot-data');
        applySpotNodeAdmission(node);
        node.bind(dataBindEndpoint);
        spot = node.createSpot();
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        applyAutoHwmMsgUnit(controlPub, options.msgSize);
        applyAutoHwmMsgUnit(controlSub, options.msgSize);
        controlPub.bind(options.controlEndpoint);
        controlSub.setSubscription(CONTROL_TOPIC);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
        emitMultiSpotNodeHwmSnapshot(node, 'spotnode_data', options.transport, options.msgSize);
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
                await sleepImmediate();
            }
        }
        if (stopRequested) {
            return;
        }
        for (;;) {
            if (trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`START,${options.msgSize}`))) {
                break;
            }
            await controlPubWaiter.wait(POLLOUT);
        }
        const runId = createRunId(1);
        const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(options.duration * 1_000_000_000));
        const latencyOnly = latencyOnlyEnabled();
        const latencyIntervalUs = integerEnv('PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US', 1000);
        let seq = 1n;
        while (process.hrtime.bigint() < activeStopNs) {
            stampPayload(payload, { phase: 1, runId, msgSize: options.msgSize, seq });
            if (trySpotPublish(spot, '', TOPIC, payload)) {
                seq += 1n;
                if (latencyOnly) {
                    await sleepMicroseconds(latencyIntervalUs);
                }
                continue;
            }
            await sleepImmediate();
        }
        stampPayload(payload, { phase: 2, runId, msgSize: options.msgSize, seq });
        for (;;) {
            if (trySpotPublish(spot, '', TOPIC, payload)) {
                break;
            }
            await sleepImmediate();
        }
    }
    finally {
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
