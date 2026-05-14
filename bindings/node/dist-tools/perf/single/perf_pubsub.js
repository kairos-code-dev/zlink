// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createPayload, createMetricCollector, createRunId, decodeMetricHeaderFromParts, currentEpochNs, summarizeMetrics, stampPayload, } = require('../common/perf_metrics');
const { applyContextPolicy, applyAutoHwmMsgUnit, applySocketPolicy, benchmarkEndpoint, configureTlsClient, configureTlsServer, emitSingleSocketHwmDetail, parseSingleBinaryArgs, resolveSingleLatencySampleCap, waitForPostReadySettle, waitForMonitorConnectionReady, } = require('./perf_single_common');
const { isStopTokenParts, STOP_TOKEN_BYTES } = require('../perf_stop_token');
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
    const pubMonitor = pub.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const subMonitor = sub.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
    const endpoint = await benchmarkEndpoint(options.transport, `pubsub-${msgSize}`);
    const topic = 'bench';
    function subscribeNoWait() {
        try {
            return sub.subscribe(zlink.RecvFlags.DontWait);
        }
        catch (error) {
            if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
                return null;
            }
            throw error;
        }
    }
    function drain(collector) {
        while (true) {
            const received = subscribeNoWait();
            if (!received) {
                return false;
            }
            if (isStopTokenParts(received.parts)) {
                return true;
            }
            const header = decodeMetricHeaderFromParts(received.parts);
            collector.record(header, currentEpochNs());
        }
    }
    function publishPayload(payload, flags = zlink.SendFlags.DontWait) {
        try {
            return pub.publish(topic).message(payload).flags(flags).submit();
        }
        catch (error) {
            const text = String(error && error.message ? error.message : error);
            if ((error instanceof zlink.SubmitError
                && (error.result === zlink.SubmitResult.Backpressured
                    || error.result === zlink.SubmitResult.NotConnected
                    || error.result === zlink.SubmitResult.NotFound))
                || (error && error.code === 'EAGAIN')
                || text.includes('Resource temporarily unavailable')) {
                return false;
            }
            throw error;
        }
    }
    try {
        applySocketPolicy(pub, {
            ...options,
            noDrop: process.env.PERF_SINGLE_PUBSUB_XPUB_NODROP === '0'
                ? false
                : true
        });
        applySocketPolicy(sub, {
            ...options,
            recvTimeout: Number(process.env.PERF_SINGLE_PUBSUB_RCVTIMEO_MS
                ?? process.env.PERF_SINGLE_RCVTIMEO_MS
                ?? 200)
        });
        applyAutoHwmMsgUnit(pub, msgSize);
        applyAutoHwmMsgUnit(sub, msgSize);
        ctx.recalculateAutoHwm();
        configureTlsServer(pub, options.transport);
        configureTlsClient(sub, options.transport);
        sub.setSubscription('');
        pub.bind(endpoint);
        sub.connect(endpoint);
        await waitForMonitorConnectionReady(subMonitor);
        await waitForMonitorConnectionReady(pubMonitor);
        trace('connection ready');
        await waitForPostReadySettle(Number(process.env.PERF_SINGLE_PUBSUB_READY_SETTLE_MS ?? 1000));
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs
            + BigInt(Math.floor(options.duration * 1_000_000_000));
        const runId = createRunId(options.runId ?? 1);
        const payload = createPayload(msgSize);
        const collector = createMetricCollector({
            runId,
            msgSize,
            activeStartNs,
            activeStopNs,
            sampleCap: resolveSingleLatencySampleCap()
        });
        let seq = 1n;
        while (currentEpochNs() < activeStopNs) {
            stampPayload(payload, { phase: 1, runId, msgSize, seq });
            if (publishPayload(payload)) {
                seq += 1n;
            }
            drain(collector);
        }
        stampPayload(payload, { phase: 2, runId, msgSize, seq });
        publishPayload(payload);
        for (let retry = 0; retry < 100; retry += 1) {
            if (publishPayload(STOP_TOKEN_BYTES, zlink.SendFlags.None)) {
                break;
            }
        }
        while (!drain(collector)) {
            // Drain until the wire-level stop token arrives.
        }
        const result = collector.finish();
        emitSingleSocketHwmDetail(pub, 'PUBSUB', options.transport, 'publisher', msgSize);
        emitSingleSocketHwmDetail(sub, 'PUBSUB', options.transport, 'subscriber', msgSize);
        return result;
    }
    finally {
        trace('closing');
        pubMonitor.close();
        subMonitor.close();
        pub.close();
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
