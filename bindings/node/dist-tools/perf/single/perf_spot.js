// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const zlink = require('@zlink-systems/zlink');
const perf_metrics_1 = require("../common/perf_metrics");
const perf_single_common_1 = require("./perf_single_common");
const perf_stop_token_1 = require("../perf_stop_token");
const TOPIC = 'bench';
function trySpotPublish(spot, payload, flags = zlink.SendFlags.DontWait) {
    try {
        return spot.publish(TOPIC)
            .message(payload)
            .flags(flags)
            .submit();
    }
    catch (error) {
        if (error instanceof zlink.SubmitError &&
            (error.result === zlink.SubmitResult.Backpressured ||
                error.result === zlink.SubmitResult.NotConnected ||
                error.result === zlink.SubmitResult.NotFound)) {
            return false;
        }
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') ||
            /Resource temporarily unavailable|temporarily unavailable|would block|Host unreachable|not connected/i.test(text)) {
            return false;
        }
        throw error;
    }
}
async function publishStopToken(spot) {
    // PERF_SINGLE_TEST_POLICY § 1.4: emit the wire-level stop token. Spot
    // stop delivery is a required phase-end signal, so failure is surfaced.
    spot.publish(TOPIC).message(perf_stop_token_1.STOP_TOKEN_BYTES).flags(zlink.SendFlags.None).submit();
}
function trySpotSubscribe(spot) {
    try {
        const received = new zlink.TopicMessage();
        return spot.subscribe(received, zlink.RecvFlags.DontWait) ? received : null;
    }
    catch (error) {
        if (error instanceof zlink.RecvError &&
            (error.result === zlink.RecvResult.NoData || error.internalErrno === 2)) {
            return null;
        }
        throw error;
    }
}
function drainSpot(spot, onMessage) {
    let processed = false;
    while (true) {
        const received = trySpotSubscribe(spot);
        if (!received) {
            return processed;
        }
        try {
            onMessage(received);
            processed = true;
        }
        finally {
            received.close();
        }
    }
}
async function runSpotBenchmark(msgSize, options) {
    const ctx = new zlink.Context();
    (0, perf_single_common_1.applyContextPolicy)(ctx);
    const publisherNode = new zlink.SpotNode(ctx);
    const subscriberNode = new zlink.SpotNode(ctx);
    let publisher = null;
    let subscriber = null;
    let stopPublisher = null;
    try {
        const publisherEndpoint = await (0, perf_single_common_1.benchmarkEndpoint)(options.transport, `spot-publisher-${msgSize}`);
        publisher = publisherNode.createSpot();
        subscriber = subscriberNode.createSpot();
        stopPublisher = subscriberNode.createSpot();
        ctx.recalculateAutoHwm();
        publisherNode.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('z-node-perf-spot-publisher')));
        subscriberNode.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('a-node-perf-spot-subscriber')));
        publisher.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('z-node-perf-spot-publisher-spot')));
        subscriber.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('a-node-perf-spot-subscriber-spot')));
        stopPublisher.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('m-node-perf-spot-stop-spot')));
        (0, perf_single_common_1.configureTlsServer)(publisherNode, options.transport);
        (0, perf_single_common_1.configureTlsClient)(publisherNode, options.transport);
        (0, perf_single_common_1.configureTlsServer)(subscriberNode, options.transport);
        (0, perf_single_common_1.configureTlsClient)(subscriberNode, options.transport);
        (0, perf_single_common_1.applySpotNodeAdmission)(publisherNode, options);
        (0, perf_single_common_1.applySpotNodeAdmission)(subscriberNode, options);
        ctx.recalculateAutoHwm();
        publisherNode.bind(publisherEndpoint);
        subscriberNode.connectPeer(publisherEndpoint);
        subscriber.setSubscription(TOPIC);
        const runId = (0, perf_metrics_1.createRunId)(options.runId ?? 1);
        const payload = (0, perf_metrics_1.createPayload)(msgSize);
        const payloadSize = Math.max(msgSize, perf_metrics_1.HEADER_SIZE);
        let seq = 1n;
        let probeReady = false;
        let stopReceived = false;
        const activeDeadline = { valueNs: 0n };
        const latenciesNs = [];
        let accepted = 0;
        const collectReadable = (countActive) => {
            return drainSpot(subscriber, (received) => {
                // PERF_SINGLE_TEST_POLICY § 1.4: wire-level stop token terminates
                // the receiver loop. Returning here lets the outer loop observe
                // `stopReceived` without recording the sentinel as a payload.
                if ((0, perf_stop_token_1.isStopTokenParts)(received.parts)) {
                    stopReceived = true;
                    return;
                }
                const header = (0, perf_metrics_1.decodeMetricHeaderFromParts)(received.parts, payloadSize);
                if (!header) {
                    return;
                }
                if (!probeReady &&
                    header.phase === 0 &&
                    header.runId === runId &&
                    header.msgSize === msgSize) {
                    probeReady = true;
                    return;
                }
                if (!countActive ||
                    header.phase !== 1 ||
                    header.runId !== runId ||
                    header.msgSize !== msgSize) {
                    return;
                }
                // Finding 4 / C bindings/c/perf/single/src/perf_spot.cpp
                // run_active_window (~462-463): only count a phase=active sample
                // when the receive time is still inside the active window
                // (`steady_clock::now() < deadline`). Mirrors the shared single
                // collector recvTs<=activeStop guard (perf_measurement.ts ~280).
                const nowNs = (0, perf_metrics_1.currentEpochNs)();
                if (activeDeadline.valueNs !== 0n && nowNs > activeDeadline.valueNs) {
                    return;
                }
                accepted += 1;
                const sentTsNs = BigInt(header.sentTsNs);
                if (nowNs >= sentTsNs) {
                    latenciesNs.push(Number(nowNs - sentTsNs));
                }
            });
        };
        const readyDefaultMs = options.transport === 'tls' || options.transport === 'wss'
            ? 10000
            : 5000;
        const readyDeadline = Date.now() + Number(process.env.PERF_SINGLE_SPOT_SUBJECT_READY_TIMEOUT_MS
            ?? process.env.PERF_CONNECT_READY_TIMEOUT_MS
            ?? readyDefaultMs);
        while (!probeReady && Date.now() < readyDeadline) {
            (0, perf_metrics_1.stampPayload)(payload, {
                phase: 0,
                runId,
                msgSize,
                seq
            });
            if (trySpotPublish(publisher, payload)) {
                seq += 1n;
            }
            collectReadable(false);
            await (0, perf_metrics_1.sleepImmediate)();
        }
        if (!probeReady) {
            throw new Error('spot ready probe timed out');
        }
        await (0, perf_single_common_1.waitForPostReadySettle)(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));
        // Finding 2 / C bindings/c/perf/single/src/perf_spot.cpp
        // run_active_window (~423-525): C runs a BLOCKING publisher thread
        // (send_spot_samples ~388-421: ZLINK_DONTWAIT publish, yield on
        // backpressure, every accepted publish counts) plus a SEPARATE
        // receiver thread (poller.wait then DONTWAIT subscribe drain, count
        // while now < deadline). Node spot handles cannot be shared across
        // Worker threads, so the faithful single-context adaptation is a
        // tight publish+drain loop with NO `sleepImmediate()` event-loop-turn
        // gating: the throughput-count anchor is HWM-backpressure driven
        // exactly like C (publish retried through backpressure by draining
        // the subscriber — the Node stand-in for C's concurrent receiver
        // thread — and `seq`/sent advancing only on an accepted publish, no
        // silent drop). The active deadline uses the same epoch clock as the
        // recv-time guard so the count window matches C's `now < deadline`.
        const activeStartNs = (0, perf_metrics_1.currentEpochNs)();
        activeDeadline.valueNs = activeStartNs
            + BigInt(Math.floor(options.duration * 1_000_000_000));
        while ((0, perf_metrics_1.currentEpochNs)() < activeDeadline.valueNs && !stopReceived) {
            (0, perf_metrics_1.stampPayload)(payload, {
                phase: 1,
                runId,
                msgSize,
                seq
            });
            if (trySpotPublish(publisher, payload)) {
                seq += 1n;
            }
            // Drain the subscriber inline (relieves SPOT backpressure, the Node
            // equivalent of C's concurrent receiver thread); the deadline guard
            // inside collectReadable bounds counted samples to the window.
            collectReadable(true);
        }
        // C perf_spot.cpp run_active_window: after the active deadline the
        // sender immediately publishes the stop token via the dedicated stop
        // publisher (no post-active catch-up phase); the receiver then drains
        // remaining in-flight payloads until it observes the stop token.
        //
        // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end via wire stop token
        // and drain in-flight payloads until the receiver observes it. The
        // legacy phase-2 cooldown message + timer-based idle drain are no
        // longer needed — in-flight messages naturally precede the token.
        await publishStopToken(stopPublisher);
        while (!stopReceived) {
            if (!collectReadable(false)) {
                await (0, perf_metrics_1.sleepImmediate)();
            }
        }
        if (accepted <= 0) {
            throw new Error('spot benchmark produced no measured messages');
        }
        (0, perf_single_common_1.emitSingleSocketHwmDetail)(publisherNode, 'SPOT', options.transport, 'publisher_node', msgSize);
        (0, perf_single_common_1.emitSingleSocketHwmDetail)(subscriberNode, 'SPOT', options.transport, 'subscriber_node', msgSize);
        return {
            latenciesNs,
            accepted
        };
    }
    finally {
        if (subscriber) {
            subscriber.close();
        }
        if (publisher) {
            publisher.close();
        }
        if (stopPublisher) {
            stopPublisher.close();
        }
        subscriberNode.close();
        publisherNode.close();
        ctx.close();
    }
}
module.exports = { runSpotBenchmark };
if (require.main === module) {
    (async () => {
        const options = (0, perf_single_common_1.parseSingleBinaryArgs)(process.argv.slice(2));
        const result = await runSpotBenchmark(options.msgSize, options);
        for (const line of (0, perf_metrics_1.summarizeMetrics)('SPOT', options.transport, options.msgSize, result.latenciesNs, options.duration, options.libName, result.accepted)) {
            console.log(line);
        }
    })().catch((error) => {
        console.error(error);
        process.exitCode = 1;
    });
}
