// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist/canonical');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeaderFromParts, currentEpochNs, sleepImmediate, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { applyContextPolicy, applySocketPolicy, benchmarkEndpoint, closeSenderWorker, configureTlsClient, configureTlsServer, drainRecvSocket, parseSingleBinaryArgs, resolveSingleLatencySampleCap, resolveSingleIdleDrainMs, spawnSenderWorker, waitForWorkerDone, waitForWorkerError, waitForMonitorConnectionReady, waitForWorkerMessage, } = require('./perf_single_common');
async function runDealerRouterBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    applyContextPolicy(ctx);
    const router = new zlink.RouterSocket(ctx);
    const routerMonitor = router.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    const endpoint = await benchmarkEndpoint(options.transport, `dealer-router-${msgSize}`);
    let worker = null;
    try {
        applySocketPolicy(router, options);
        configureTlsServer(router, options.transport);
        router.bind(endpoint);
        worker = spawnSenderWorker({
            kind: 'dealer_router',
            transport: options.transport,
            endpoint,
            duration: options.duration,
            msgSize,
            runId: options.runId ?? 1,
            options,
        });
        const workerError = waitForWorkerError(worker);
        await Promise.race([
            waitForWorkerMessage(worker, 'ready'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        await waitForMonitorConnectionReady(routerMonitor);
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
        let stop = false;
        const recvTask = drainRecvSocket(router, (received) => {
            const header = decodeMetricHeaderFromParts(received.parts);
            collector.record(header, currentEpochNs());
        }, () => stop);
        worker.postMessage({ type: 'start' });
        await Promise.race([
            waitForWorkerDone(worker, options.duration),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        const drainDeadlineNs = activeStopNs
            + BigInt(resolveSingleIdleDrainMs(options)) * 1000000n;
        while (currentEpochNs() < drainDeadlineNs) {
            await sleepImmediate();
        }
        stop = true;
        await recvTask;
        return collector.finish();
    }
    finally {
        await closeSenderWorker(worker);
        routerMonitor.close();
        router.close();
        ctx.close();
    }
}
module.exports = { runDealerRouterBenchmark };
if (require.main === module) {
    (async () => {
        const options = parseSingleBinaryArgs(process.argv.slice(2));
        const result = await runDealerRouterBenchmark(options.msgSize, options);
        for (const line of summarizeMetrics('DEALER_ROUTER', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
