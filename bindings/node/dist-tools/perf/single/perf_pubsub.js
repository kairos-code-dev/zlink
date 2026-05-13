// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, summarizeMetrics, } = require('../common/perf_metrics');
const { applyContextPolicy, applySocketPolicy, benchmarkEndpoint, closeSenderWorker, drainRecvSocket, parseSingleBinaryArgs, resolveSingleLatencySampleCap, spawnSenderWorker, waitForPostReadySettle, waitForConnectionReady, waitForWorkerDone, waitForWorkerError, waitForWorkerMessage, } = require('./perf_single_common');
function trace(message) {
    if (process.env.PERF_NODE_TRACE === '1') {
        console.error(`[pubsub] ${message}`);
    }
}
async function runPubSubBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    applyContextPolicy(ctx);
    const pub = new zlink.PubSocket(ctx);
    const sub = new zlink.SubSocket(ctx);
    const endpoint = await benchmarkEndpoint(options.transport, `pubsub-${msgSize}`);
    const topic = 'perf:pubsub';
    let worker = null;
    try {
        applySocketPolicy(sub, {
            ...options,
            recvTimeout: Number(process.env.PERF_SINGLE_PUBSUB_RCVTIMEO_MS
                ?? process.env.PERF_SINGLE_RCVTIMEO_MS
                ?? 200)
        });
        worker = spawnSenderWorker({
            kind: 'pubsub',
            transport: options.transport,
            endpoint,
            duration: options.duration,
            msgSize,
            runId: options.runId ?? 1,
            topic,
            options: {
                ...options,
                // PERF_SINGLE_TEST_POLICY § 1.4 needs the wire-level stop token to
                // be reliably delivered. Default the publisher to no_drop=true so
                // the sentinel is not silently discarded by the XPUB drop policy
                // (matches cpp `publisher.options().no_drop(true)` in
                // `bindings/cpp/perf/single/src/perf_pubsub.cpp`).
                noDrop: process.env.PERF_SINGLE_PUBSUB_XPUB_NODROP === '0'
                    ? false
                    : true
            },
        });
        const workerError = waitForWorkerError(worker);
        trace('waiting for worker bound');
        await Promise.race([
            waitForWorkerMessage(worker, 'bound'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        trace('worker bound');
        sub.setSubscription(topic);
        await waitForConnectionReady(sub, () => sub.connect(endpoint));
        trace('sub connection ready');
        await waitForPostReadySettle(Number(process.env.PERF_SINGLE_PUBSUB_READY_SETTLE_MS ?? 1000));
        worker.postMessage({ type: 'ready' });
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs
            + BigInt(Math.floor(options.duration * 1_000_000_000));
        const runId = createRunId(options.runId ?? 1);
        const collector = createMetricCollector({
            runId,
            msgSize,
            activeStartNs,
            activeStopNs,
            sampleCap: resolveSingleLatencySampleCap()
        });
        // PERF_SINGLE_TEST_POLICY § 1.4: receiver drains until wire stop token.
        const recvTask = drainRecvSocket(sub, (received) => {
            const header = decodeMetricHeaderFromParts(received.parts);
            collector.record(header, currentEpochNs());
        });
        trace('starting worker');
        worker.postMessage({ type: 'start' });
        await Promise.race([
            waitForWorkerDone(worker, options.duration),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        trace('worker done');
        await recvTask;
        trace('recv task done');
        return collector.finish();
    }
    finally {
        trace('closing');
        await closeSenderWorker(worker);
        trace('worker closed');
        sub.close();
        trace('sub closed');
        if (process.env.PERF_SINGLE_PUBSUB_CLOSE_CONTEXT === '1') {
            ctx.close();
            trace('ctx closed');
        }
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
