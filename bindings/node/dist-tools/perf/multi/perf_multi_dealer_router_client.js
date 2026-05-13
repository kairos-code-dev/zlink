// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, currentEpochNs, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, pollEvents, pollEventHas, recvNoWaitInto, resolveMultiLatencySampleCap, sendStopTokenWithRetry, trySocketSend, waitForConnectionReady } = require('./perf_multi_runtime');
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_DEALER_ROUTER');
    const dealers = [];
    const payloads = [];
    const replyBuffers = [];
    const waiting = [];
    const sendPending = [];
    const poller = new zlink.Poller();
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const dealer = new zlink.DealerSocket(ctx);
            applySocketPolicy(dealer);
            dealer.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`CLIENT-${i}`, 'ascii')));
            dealers.push(dealer);
            payloads.push(createPayload(options.msgSize));
            replyBuffers.push(new zlink.Received());
            waiting.push(false);
            sendPending.push(false);
        }
        for (let i = 0; i < dealers.length; i += 1) {
            await waitForConnectionReady(dealers[i], () => dealers[i].connect(options.endpoint));
            applyAutoHwmMsgUnit(dealers[i], options.msgSize);
            poller.add(dealers[i], pollEvents(POLLIN | POLLOUT), i);
        }
        ctx.recalculateAutoHwm();
        const runId = createRunId(1);
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        const collector = createMetricCollector({
            runId,
            msgSize: options.msgSize,
            activeStartNs,
            activeStopNs,
            roundTrip: true,
            sampleCap: resolveMultiLatencySampleCap()
        });
        let seq = 1n;
        const drainReply = (index) => {
            let progressed = false;
            while (true) {
                const echoed = replyBuffers[index];
                if (!recvNoWaitInto(dealers[index], echoed)) {
                    break;
                }
                waiting[index] = false;
                collector.record(decodeMetricHeader(echoed.parts[0].data()), currentEpochNs());
                progressed = true;
            }
            return progressed;
        };
        while (currentEpochNs() < activeStopNs) {
            let progressed = false;
            for (let i = 0; i < dealers.length; i += 1) {
                if (waiting[i] || sendPending[i]) {
                    continue;
                }
                stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                if (!trySocketSend(dealers[i], payloads[i])) {
                    sendPending[i] = true;
                    continue;
                }
                waiting[i] = true;
                seq += 1n;
                progressed = true;
            }
            for (let i = 0; i < dealers.length; i += 1) {
                progressed = drainReply(i) || progressed;
            }
            if (progressed) {
                continue;
            }
            // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven `-1` wait. The echo
            // reply (POLLIN) or send-readiness (POLLOUT) wakeup arrives via core,
            // so timer-bound polling is unnecessary.
            const ready = poller.waitMany(poller.size, -1);
            if (ready.length === 0) {
                continue;
            }
            for (const event of ready) {
                const index = event.tag ?? event.userData;
                if (!Number.isInteger(index)) {
                    continue;
                }
                if (pollEventHas(event, POLLOUT)) {
                    sendPending[index] = false;
                }
                if (pollEventHas(event, POLLIN)) {
                    drainReply(index);
                }
            }
        }
        // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end to the echo server
        // via the wire-level stop token. The server's recv loop exits on the
        // first stop token observed.
        await sendStopTokenWithRetry(dealers[0], (bytes) => trySocketSend(dealers[0], bytes));
        const result = await collector.finish();
        for (const metricLine of summarizeMetrics('MULTI_DEALER_ROUTER', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
    }
    finally {
        poller.close();
        for (const reply of replyBuffers) {
            reply.close();
        }
        for (const dealer of dealers) {
            dealer.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
