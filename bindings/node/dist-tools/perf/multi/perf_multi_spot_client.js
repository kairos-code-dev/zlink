// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('../../dist');
const { createMetricCollector, decodeMetricHeader, summarizeMetrics } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const TOPIC = 'perf.topic';
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    const collector = createMetricCollector({
        runId: 0,
        msgSize: options.msgSize
    });
    const slots = [];
    let stop = false;
    let stopResolve;
    let timeoutId = null;
    let firstHeaderLogged = false;
    const stopped = new Promise((resolve) => {
        stopResolve = resolve;
    });
    const deadlineReached = new Promise((resolve) => {
        timeoutId = setTimeout(resolve, Math.ceil((options.warmup + options.duration + 2) * 1000));
    });
    const handleDelivery = (received) => {
        const header = decodeMetricHeader(received.parts[0].toBuffer());
        if (!header) {
            return;
        }
        if (!firstHeaderLogged) {
            firstHeaderLogged = true;
            console.error(`spot client first header: phase=${header.phase} msgSize=${header.msgSize} runId=${header.runId}`);
        }
        if (header.phase === 1) {
            stopResolve();
            return;
        }
        collector.record(header, process.hrtime.bigint());
    };
    const waitForSubDeliveryReady = async (slot, timeoutMs) => {
        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline) {
            if (slot.node.statusSnapshot().readySubjectCount > 0) {
                return;
            }
            await new Promise((resolve) => setImmediate(resolve));
        }
        throw new Error(`spot client ready timeout: ${JSON.stringify({
            status: slot.node.statusSnapshot(),
            peers: slot.node.peersSnapshot(),
            subjects: slot.node.subjectsSnapshot()
        })}`);
    };
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const node = new zlink.SpotNode(ctx);
            const spot = new zlink.Spot(node);
            slots.push({ node, spot });
        }
        for (const slot of slots) {
            slot.spot.setLinger(0);
            slot.spot.setReceiveHighWaterMark(100);
            slot.spot.setReceiveTimeout(200);
            slot.node.connectPeer(options.endpoint);
            slot.spot.setSubscription(TOPIC);
            if (options.recv === 'callback') {
                slot.spot.onSubscribe((routingId, topic, parts) => {
                    void routingId;
                    void topic;
                    handleDelivery({ parts });
                });
            }
            else {
                (async () => {
                    while (!stop) {
                        let received = null;
                        try {
                            received = slot.spot.trySubscribe();
                        }
                        catch (_) {
                            await new Promise((resolve) => setImmediate(resolve));
                            continue;
                        }
                        if (!received) {
                            await new Promise((resolve) => setImmediate(resolve));
                            continue;
                        }
                        handleDelivery(received);
                    }
                })();
            }
        }
        for (const slot of slots) {
            await waitForSubDeliveryReady(slot, 10000);
            console.error(`spot client ready: ${JSON.stringify({
                status: slot.node.statusSnapshot(),
                peers: slot.node.peersSnapshot(),
                subjects: slot.node.subjectsSnapshot()
            })}`);
        }
        console.log(`CLIENT_READY,${options.msgSize}`);
        await Promise.race([
            stopped,
            deadlineReached
        ]);
        stop = true;
        if (timeoutId) {
            clearTimeout(timeoutId);
            timeoutId = null;
        }
        const result = await collector.finish();
        const lines = summarizeMetrics('MULTI_SPOT', 'tcp', options.msgSize, result.latenciesUs, options.duration);
        for (const line of lines) {
            console.log(line);
        }
    }
    finally {
        if (timeoutId) {
            clearTimeout(timeoutId);
            timeoutId = null;
        }
        await collector.close();
        for (const slot of slots) {
            slot.spot.close();
            slot.node.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
