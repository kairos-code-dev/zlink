// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, summarizeMetrics, } = require('../common/perf_metrics');
const { applyContextPolicy, applyAutoHwmMsgUnit, applySocketPolicy, benchmarkEndpoint, closeSenderWorker, drainRecvSocket, emitSingleSocketHwmDetail, parseSingleBinaryArgs, spawnSenderWorker, waitForWorkerDone, waitForWorkerError, waitForPostReadySettle, waitForMonitorConnectionReady, waitForWorkerMessage, } = require('./perf_single_common');
function trace(message) {
    if (process.env.PERF_NODE_TRACE === '1') {
        console.error(`[pubsub] ${message}`);
    }
}
async function runPubSubBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    applyContextPolicy(ctx);
    const sub = new zlink.SubSocket(ctx);
    const subMonitor = sub.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const endpoint = await benchmarkEndpoint(options.transport, `pubsub-${msgSize}`);
    let worker = null;
    try {
        applySocketPolicy(sub, {
            ...options,
            recvTimeout: Number(process.env.PERF_SINGLE_PUBSUB_RCVTIMEO_MS
                ?? process.env.PERF_SINGLE_RCVTIMEO_MS
                ?? 200)
        });
        applyAutoHwmMsgUnit(sub, msgSize);
        ctx.recalculateAutoHwm();
        sub.setSubscription('');
        sub.connect(endpoint);
        worker = spawnSenderWorker({
            kind: 'pubsub',
            transport: options.transport,
            endpoint,
            duration: options.duration,
            msgSize,
            runId: options.runId ?? 1,
            topic: 'bench',
            options: {
                ...options,
                noDrop: process.env.PERF_SINGLE_PUBSUB_XPUB_NODROP === '0'
                    ? false
                    : true
            },
        });
        const workerError = waitForWorkerError(worker);
        await Promise.race([
            waitForWorkerMessage(worker, 'bound'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        worker.postMessage({ type: 'ready' });
        await waitForMonitorConnectionReady(subMonitor);
        trace('connection ready');
        await waitForPostReadySettle(Number(process.env.PERF_SINGLE_PUBSUB_READY_SETTLE_MS ?? 1000));
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs
            + BigInt(Math.floor(options.duration * 1_000_000_000));
        const runId = createRunId(options.runId ?? 1);
        const collector = createMetricCollector({
            runId,
            msgSize,
            activeStartNs,
            activeStopNs,
        });
        worker.postMessage({ type: 'start' });
        await Promise.race([
            waitForWorkerMessage(worker, 'started'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        const recvTask = drainRecvSocket(sub, (received) => {
            const header = decodeMetricHeaderFromParts(received.parts);
            collector.record(header, currentEpochNs());
        });
        await Promise.race([
            waitForWorkerDone(worker, options.duration),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        await recvTask;
        const result = collector.finish();
        emitSingleSocketHwmDetail(sub, 'PUBSUB', options.transport, 'subscriber', msgSize);
        return result;
    }
    finally {
        trace('closing');
        await closeSenderWorker(worker);
        subMonitor.close();
        sub.close();
        trace('sub closed');
        ctx.close();
        trace('ctx closed');
    }
}
module.exports = { runPubSubBenchmark };
if (require.main === module) {
    (async () => {
        const options = parseSingleBinaryArgs(process.argv.slice(2));
        const result = await runPubSubBenchmark(options.msgSize, options);
        for (const line of summarizeMetrics('PUBSUB', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
