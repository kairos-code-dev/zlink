// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const path = require('node:path');
const { Worker } = require('node:worker_threads');
const zlink = require('@zlink-systems/zlink');
const { currentEpochNs, sleepMillis, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs, resolveMultiSpotControlSettleMs, resolveMultiSpotReadySettleMs } = require('./perf_multi_common');
const { POLLOUT, POLLIN, applyAutoHwmMsgUnit, applySocketPolicy, applyContextPolicy, createSocketEventWaiter, emitMultiSocketHwmDetail, publishControlUntilSent, trySocketPublish, waitForControlStart, waitForRunnerControlConnected, waitForRunnerStart } = require('./perf_multi_runtime');
const CONTROL_TOPIC = 'bench';
const TRACE = process.env.PERF_MULTI_SPOT_TRACE === '1';
function trace(message) {
    if (TRACE) {
        console.error(`[multi-spot-client] ${message}`);
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
function integerEnv(name, fallback) {
    const value = Number(process.env[name]);
    return Number.isFinite(value) ? Math.trunc(value) : fallback;
}
function resolveSpotRecvWorkerCount(slotCount) {
    if (slotCount <= 0) {
        return 0;
    }
    const configured = integerEnv('PERF_MULTI_SPOT_RECV_WORKERS', 0);
    if (configured > 0) {
        return Math.min(slotCount, configured);
    }
    const scaled = Math.max(4, Math.min(128, Math.ceil(slotCount / 16)));
    return Math.min(slotCount, scaled);
}
function onceWorkerMessage(worker, expectedType, timeoutMs) {
    return new Promise((resolve, reject) => {
        let done = false;
        const timeout = setTimeout(() => {
            if (done) {
                return;
            }
            done = true;
            reject(new Error(`worker timeout waiting for ${expectedType}`));
        }, timeoutMs);
        const cleanup = () => {
            clearTimeout(timeout);
            worker.off('message', onMessage);
            worker.off('error', onError);
            worker.off('exit', onExit);
        };
        const onMessage = (message) => {
            if (!message || message.type !== expectedType) {
                if (message && message.type === 'error') {
                    done = true;
                    cleanup();
                    reject(new Error(message.error));
                }
                return;
            }
            done = true;
            cleanup();
            resolve(message);
        };
        const onError = (error) => {
            if (done) {
                return;
            }
            done = true;
            cleanup();
            reject(error);
        };
        const onExit = (code) => {
            if (done || code === 0) {
                return;
            }
            done = true;
            cleanup();
            reject(new Error(`worker exited before ${expectedType}: ${code}`));
        };
        worker.on('message', onMessage);
        worker.once('error', onError);
        worker.once('exit', onExit);
    });
}
function spawnRecvWorkers(options) {
    const workerCount = resolveSpotRecvWorkerCount(Math.trunc(options.clients));
    const workers = [];
    let remainingClients = Math.trunc(options.clients);
    for (let i = 0; i < workerCount; i += 1) {
        const remainingWorkers = workerCount - i;
        const clients = Math.ceil(remainingClients / remainingWorkers);
        remainingClients -= clients;
        workers.push(new Worker(path.join(__dirname, 'perf_multi_spot_recv_worker.js'), {
            workerData: {
                workerIndex: i,
                clients,
                transport: options.transport,
                peerEndpoint: options.peerEndpoint,
                msgSize: options.msgSize,
                duration: options.duration,
            }
        }));
    }
    return workers;
}
async function closeWorkers(workers) {
    await Promise.allSettled(workers.map((worker) => worker.terminate()));
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_SPOT');
    const controlPub = new zlink.PubSocket(ctx);
    const controlSub = new zlink.SubSocket(ctx);
    const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
    const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
    let workers = [];
    try {
        applySocketPolicy(controlPub);
        applySocketPolicy(controlSub);
        applyAutoHwmMsgUnit(ctx, options.msgSize);
        ctx.recalculateAutoHwm();
        emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
        emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
        controlPub.bind(options.controlEndpoint);
        console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
        controlSub.setSubscription(CONTROL_TOPIC);
        controlSub.connect(options.serverControlEndpoint);
        await waitForRunnerControlConnected();
        trace('control-connected');
        trace(`creating-recv-workers clients=${options.clients}`);
        workers = spawnRecvWorkers(options);
        await Promise.all(workers.map((worker) => onceWorkerMessage(worker, 'ready', Number(process.env.PERF_CONNECT_READY_TIMEOUT_MS || 1000))));
        trace(`recv-workers-ready count=${workers.length}`);
        const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
        while (Date.now() < stabilizationDeadline) {
            tryControlPublish(controlPub, 'CONNECTED');
            await sleepMillis(Math.min(50, Math.max(1, stabilizationDeadline - Date.now())));
        }
        await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
        await sleepMillis(resolveMultiSpotControlSettleMs());
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
                sleepMillis(Math.min(50, Math.max(1, nextReadyAt - Date.now())))
            ]);
        }
        trace('start-handshake-done');
        const controlStartNs = currentEpochNs();
        trace('recv-workers-start');
        const resultWaits = workers.map((worker) => onceWorkerMessage(worker, 'result', Math.ceil(options.duration * 1000 * 6) + 5000));
        for (const worker of workers) {
            worker.postMessage({
                type: 'start',
                activeStartNs: controlStartNs.toString(),
            });
        }
        const workerResults = await Promise.all(resultWaits);
        trace('recv-workers-done');
        const result = workerResults.reduce((merged, item) => {
            merged.accepted += item.accepted || 0;
            if (Array.isArray(item.latenciesNs)) {
                for (let i = 0; i < item.latenciesNs.length; i += 1) {
                    merged.latenciesNs.push(item.latenciesNs[i]);
                }
            }
            return merged;
        }, { accepted: 0, latenciesNs: [] });
        for (const metricLine of summarizeMetrics('MULTI_SPOT', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
        trace('result-flushed');
        await new Promise((resolve) => process.stdout.write('', resolve));
        process.exit(0);
    }
    finally {
        await closeWorkers(workers);
        controlPubWaiter.close();
        controlSubWaiter.close();
        closeQuietly(controlSub);
        closeQuietly(controlPub);
        closeQuietly(ctx);
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
