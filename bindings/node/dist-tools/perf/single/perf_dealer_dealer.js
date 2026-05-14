// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, summarizeMetrics, } = require('../common/perf_metrics');
const { applyContextPolicy, applyAutoHwmMsgUnit, applySocketPolicy, benchmarkEndpoint, closeSenderWorker, configureTlsServer, drainRecvSocket, emitSingleSocketHwmDetail, parseSingleBinaryArgs, runLocalSocketOneWayBenchmark, spawnSenderWorker, waitForWorkerDone, waitForWorkerError, waitForMonitorConnectionReady, waitForWorkerMessage, } = require('./perf_single_common');
async function runDealerDealerBenchmark(msgSize, options) {
    if (options.transport === 'inproc') {
        return runLocalSocketOneWayBenchmark({
            pattern: 'DEALER_DEALER',
            msgSize,
            options,
            endpointToken: 'dealer-dealer',
            createReceiver: (ctx) => new zlink.DealerSocket(ctx),
            createSender: (ctx) => new zlink.DealerSocket(ctx),
        });
    }
    const ctx = new zlink.Context();
    applyContextPolicy(ctx);
    const server = new zlink.DealerSocket(ctx);
    const serverMonitor = server.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const endpoint = await benchmarkEndpoint(options.transport, `dealer-dealer-${msgSize}`);
    let worker = null;
    try {
        applySocketPolicy(server, options);
        applyAutoHwmMsgUnit(server, msgSize);
        ctx.recalculateAutoHwm();
        configureTlsServer(server, options.transport);
        server.bind(endpoint);
        worker = spawnSenderWorker({
            kind: 'dealer_dealer',
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
        await waitForMonitorConnectionReady(serverMonitor);
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
        // PERF_SINGLE_TEST_POLICY § 1.4: receiver drains until the wire stop
        // token arrives — in-flight payloads precede it naturally, so the
        // explicit deadline-based drain is no longer needed.
        worker.postMessage({ type: 'start' });
        await Promise.race([
            waitForWorkerMessage(worker, 'started'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        const recvTask = drainRecvSocket(server, (received) => {
            const header = decodeMetricHeaderFromParts(received.parts);
            collector.record(header, currentEpochNs());
        });
        await Promise.race([
            waitForWorkerDone(worker, options.duration),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        await recvTask;
        const result = collector.finish();
        emitSingleSocketHwmDetail(server, 'DEALER_DEALER', options.transport, 'receiver', msgSize);
        return result;
    }
    finally {
        await closeSenderWorker(worker);
        serverMonitor.close();
        server.close();
        ctx.close();
    }
}
module.exports = { runDealerDealerBenchmark };
if (require.main === module) {
    (async () => {
        const options = parseSingleBinaryArgs(process.argv.slice(2));
        const result = await runDealerDealerBenchmark(options.msgSize, options);
        for (const line of summarizeMetrics('DEALER_DEALER', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
