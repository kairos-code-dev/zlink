// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../..');
const { createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, summarizeMetrics, } = require('../common/perf_metrics');
const { applyContextPolicy, applySocketPolicy, benchmarkEndpoint, closeSenderWorker, configureTlsServer, drainRecvSocket, parseSingleBinaryArgs, resolveSingleLatencySampleCap, spawnSenderWorker, waitForWorkerDone, waitForWorkerError, waitForMonitorConnectionReady, waitForWorkerMessage, } = require('./perf_single_common');
const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');
const RECEIVER_ROUTING_ID = zlink.RoutingId.fromBytes(RECEIVER_ID);
const SENDER_ROUTING_ID = zlink.RoutingId.fromBytes(SENDER_ID);
function trace(message) {
    if (process.env.PERF_NODE_TRACE === '1') {
        console.error(`[router-router] ${message}`);
    }
}
function partStrings(received) {
    return received.parts.map((part) => part.data().toString());
}
async function handshakeReceiver(receiver) {
    const ping = receiver.recv();
    if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
        throw new Error('router-router handshake receive failed');
    }
    receiver.send(SENDER_ROUTING_ID, Buffer.from('PONG'));
}
async function runRouterRouterBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    applyContextPolicy(ctx);
    const receiver = new zlink.RouterSocket(ctx);
    const receiverMonitor = receiver.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const endpoint = await benchmarkEndpoint(options.transport, `router-router-${msgSize}`);
    let worker = null;
    try {
        applySocketPolicy(receiver, options);
        receiver.setRoutingId(RECEIVER_ROUTING_ID);
        configureTlsServer(receiver, options.transport);
        receiver.bind(endpoint);
        worker = spawnSenderWorker({
            kind: 'router_router',
            transport: options.transport,
            endpoint,
            duration: options.duration,
            msgSize,
            runId: options.runId ?? 1,
            receiverRoutingIdBytes: RECEIVER_ID,
            senderRoutingIdBytes: SENDER_ID,
            options,
        });
        const workerError = waitForWorkerError(worker);
        trace('waiting worker connected');
        await Promise.race([
            waitForWorkerMessage(worker, 'connected'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        trace('worker connected');
        await waitForMonitorConnectionReady(receiverMonitor);
        trace('monitor ready');
        worker.postMessage({ type: 'handshake' });
        await handshakeReceiver(receiver);
        trace('handshake receiver done');
        await Promise.race([
            waitForWorkerMessage(worker, 'ready'),
            workerError.then((message) => Promise.reject(new Error(message.message)))
        ]);
        trace('worker ready');
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
        const recvTask = drainRecvSocket(receiver, (received) => {
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
        receiverMonitor.close();
        trace('monitor closed');
        receiver.close();
        trace('receiver closed');
        ctx.close();
        trace('ctx closed');
    }
}
module.exports = { runRouterRouterBenchmark };
if (require.main === module) {
    (async () => {
        const options = parseSingleBinaryArgs(process.argv.slice(2));
        const result = await runRouterRouterBenchmark(options.msgSize, options);
        for (const line of summarizeMetrics('ROUTER_ROUTER', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
