// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const { requireNative } = require('../../dist/zlink/runtime/native/native');
const { createMetricCollector, createPayload, createRunId, decodeMetricHeader, HEADER_SIZE, currentEpochNs, summarizeMetrics, stampPayload } = require('../common/perf_metrics');
const { configureTlsClient } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const { POLLIN, POLLOUT, applyAutoHwmMsgUnit, applyContextPolicy, applySocketPolicy, emitMultiSocketHwmDetail, pollEvents, pollEventHas, recvNoWaitInto, sendStopTokenOnce, trySocketSend, waitForConnectionReady } = require('./perf_multi_runtime');
const SERVER_ID = Buffer.from('multi-router-router-server', 'ascii');
const SERVER_ROUTING_ID = zlink.RoutingId.fromBytes(SERVER_ID);
function tryBorrowedRoutingSend(socket, routingIdBytes, payload, length) {
    const result = requireNative().socketSendRoutingBorrowedNoWaitResult(socket.nativeHandle(), routingIdBytes, payload, length | 0);
    if (result === zlink.SubmitResult.Ok) {
        return true;
    }
    if (result === zlink.SubmitResult.Backpressured ||
        result === zlink.SubmitResult.NotConnected) {
        return false;
    }
    throw new zlink.SubmitError(result, 0, 'borrowed routed send failed');
}
async function main() {
    const options = parseMultiArgs(process.argv.slice(2));
    const ctx = new zlink.Context();
    applyContextPolicy(ctx, 'client', 'MULTI_ROUTER_ROUTER');
    const routers = [];
    const payloads = [];
    const replyBuffers = [];
    const replyMessages = [];
    const waiting = [];
    const sendPending = [];
    const poller = new zlink.Poller();
    try {
        for (let i = 0; i < options.clients; i += 1) {
            const router = new zlink.RouterSocket(ctx);
            applySocketPolicy(router);
            configureTlsClient(router, options.transport);
            router.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`multi-router-client-${i}`, 'ascii')));
            routers.push(router);
            payloads.push(createPayload(options.msgSize));
            replyBuffers.push(Buffer.allocUnsafe(HEADER_SIZE));
            replyMessages.push(new zlink.Received());
            waiting.push(false);
            sendPending.push(false);
        }
        for (let i = 0; i < routers.length; i += 1) {
            await waitForConnectionReady(routers[i], () => routers[i].connect(options.endpoint));
            applyAutoHwmMsgUnit(ctx, options.msgSize);
            poller.add(routers[i], pollEvents(POLLIN | POLLOUT), i);
        }
        ctx.recalculateAutoHwm();
        for (const router of routers) {
            emitMultiSocketHwmDetail(router, 'endpoint', options.transport, options.msgSize);
        }
        const runId = createRunId(1);
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        const collector = createMetricCollector({
            runId,
            msgSize: options.msgSize,
            activeStartNs,
            activeStopNs,
            roundTrip: true,
        });
        let seq = 1n;
        const drainReply = (index) => {
            let progressed = false;
            while (true) {
                if (options.msgSize >= 65536) {
                    const echoed = replyBuffers[index];
                    const received = routers[index].recvPayloadInto(echoed, zlink.RecvFlags.DontWait);
                    if (received === null) {
                        break;
                    }
                    waiting[index] = false;
                    collector.record(decodeMetricHeader(echoed.subarray(0, Math.min(received, echoed.length))), currentEpochNs());
                }
                else {
                    const echoed = replyMessages[index];
                    if (!recvNoWaitInto(routers[index], echoed)) {
                        break;
                    }
                    waiting[index] = false;
                    collector.record(decodeMetricHeader(echoed.parts[0].data()), currentEpochNs());
                }
                progressed = true;
            }
            return progressed;
        };
        while (currentEpochNs() < activeStopNs) {
            let progressed = false;
            for (let i = 0; i < routers.length; i += 1) {
                if (waiting[i] || sendPending[i]) {
                    continue;
                }
                stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
                const sent = options.transport === 'tcp'
                    ? tryBorrowedRoutingSend(routers[i], SERVER_ID, payloads[i], options.msgSize)
                    : trySocketSend(routers[i], SERVER_ROUTING_ID, payloads[i]);
                if (!sent) {
                    sendPending[i] = true;
                    continue;
                }
                waiting[i] = true;
                seq += 1n;
                progressed = true;
            }
            for (let i = 0; i < routers.length; i += 1) {
                progressed = drainReply(i) || progressed;
            }
            if (progressed) {
                continue;
            }
            // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven `-1` wait.
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
        // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end via wire stop token.
        await sendStopTokenOnce(routers[0], (bytes) => trySocketSend(routers[0], SERVER_ROUTING_ID, bytes));
        const result = await collector.finish();
        for (const metricLine of summarizeMetrics('MULTI_ROUTER_ROUTER', options.transport, options.msgSize, result.latenciesNs, options.duration, 'current', result.accepted)) {
            console.log(metricLine);
        }
    }
    finally {
        poller.close();
        for (const reply of replyMessages) {
            reply.close();
        }
        for (const router of routers) {
            router.close();
        }
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
