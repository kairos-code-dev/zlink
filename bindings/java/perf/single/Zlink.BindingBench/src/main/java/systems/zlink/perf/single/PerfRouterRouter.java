/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.MonitorEventType;
import systems.zlink.contracts.PollEventFlag;
import systems.zlink.contracts.RouterSocket;
import systems.zlink.contracts.RoutingId;
import systems.zlink.contracts.SendFlags;
import systems.zlink.contracts.SocketType;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class PerfRouterRouter {
    private static final MonitorEventType READY_EVENT = MonitorEventType.CONNECTION_READY;
    private static final RoutingId ROUTER1 = RoutingId.fromBytes(
        "ROUTER1".getBytes(StandardCharsets.UTF_8));

    private PerfRouterRouter() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-router-router");
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch routed = new CountDownLatch(1);
        AtomicBoolean probePending = new AtomicBoolean(true);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        boolean sharedContext = true;
        Context receiverCtx = PerfUtil.newContext(config);
        Context senderCtx = sharedContext ? receiverCtx : PerfUtil.newContext(config);
        try (RouterSocket receiver = new RouterSocket(receiverCtx);
             RouterSocket sender = new RouterSocket(senderCtx);
             var receiverMonitor = receiver.monitorOpen(MonitorEventType.CONNECTION_READY);
             var senderMonitor = sender.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            PerfUtil.applyMonitorOptions(receiverMonitor, config);
            PerfUtil.applyMonitorOptions(senderMonitor, config);
            PerfUtil.applySocketOptions(receiver, config);
            PerfUtil.applySocketOptions(sender, config);
            PerfUtil.applyAutoHwmMsgUnit(receiver, config.size());
            PerfUtil.applyAutoHwmMsgUnit(sender, config.size());
            PerfUtil.recalculateAutoHwm(receiverCtx);
            PerfUtil.configureServerTls(receiver, config.transport());
            PerfUtil.configureClientTls(sender, config.transport());
            receiver.setRoutingId(ROUTER1);
            sender.options().connectRoutingId(ROUTER1);
            receiver.bind(PerfUtil.bindEndpoint(endpoint, config.transport()));
            sender.connect(PerfUtil.connectedEndpoint(receiver, endpoint,
                config.transport()));
            PerfUtil.waitForMonitorEvent(senderMonitor, READY_EVENT, 1,
                readyTimeout, "router/router sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENT, 1,
                readyTimeout, "router/router receiver ready");

            // PERF_SINGLE_TEST_POLICY § 1.4: receiver waits with -1 and exits
            // on wire-level stop token.
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            Thread receiverThread = new Thread(() -> {
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(receiver), PollEventFlag.POLLIN)) {
                    while (true) {
                        pollSet.poll(-1);
                        boolean stop = false;
                        while (true) {
                            systems.zlink.contracts.Received received =
                                PerfUtil.recvNoWait(receiver);
                            if (received == null) {
                                break;
                            }
                            try (received) {
                                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                                    stop = true;
                                    break;
                                }
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header == null) {
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_WARMUP
                                    && probePending.compareAndSet(true, false)) {
                                    routed.countDown();
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_ACTIVE
                                    && System.nanoTime() < activeEnd) {
                                    metrics.recordNanos(header.latencyNanos());
                                }
                            }
                        }
                        if (stop) {
                            finished.countDown();
                            return;
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-router-router-receiver");
            receiverThread.start();

            try (Message probe = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                sender.send(ROUTER1).message(probe).submit();
            }
            PerfUtil.await(routed, "router/router self-check", Duration.ofSeconds(10));

            Thread traffic = new Thread(() -> {
                try {
                    try (Message active = PerfUtil.payloadTemplate(config.size())) {
                        while (System.nanoTime() < activeEnd) {
                            PerfUtil.resetAndWritePayload(active, config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                            try (Message outbound = Message.copyOf(active)) {
                                sender.send(ROUTER1).message(outbound).submit();
                            }
                        }
                    }
                    // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end with a
                    // wire-level stop token routed via ROUTER1 (ROUTER->ROUTER
                    // requires an explicit routing id). C parity:
                    // perf_router_router.cpp send_router_stop_token (~385-410)
                    // does a bounded retry (<=100 attempts, 1ms sleep) through
                    // transient backpressure / no-route. A single blocking
                    // submit can lose the token under load (ROUTER drops on
                    // EHOSTUNREACH/ENOTCONN), hanging the receiver on poll(-1).
                    sendRouterStopToken(sender);
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-router-router-sender");
            traffic.start();
            PerfUtil.await(finished, "router/router receiver",
                Duration.ofSeconds(config.durationSeconds() + 30L));
            PerfUtil.join(traffic, "router/router sender", Duration.ofSeconds(10));
            PerfUtil.join(receiverThread, "router/router receiver thread",
                Duration.ofSeconds(10));
            if (failure.get() != null) {
                throw new IllegalStateException("router/router receiver failed",
                    failure.get());
            }
            PerfUtil.printSingleMonitorAutoHwm(config, receiverMonitor, "receiver",
                SocketType.ROUTER);
            PerfUtil.printSingleMonitorAutoHwm(config, senderMonitor, "sender",
                SocketType.ROUTER);
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                senderCtx.close();
            }
            receiverCtx.close();
        }
    }

    // C parity: perf_router_router.cpp send_router_stop_token (~385-410) /
    // perf_single_one_way.hpp send_stop_token_with_retry (~200-215). Bounded
    // retry through transient backpressure / missing route so the single stop
    // token reaches the ROUTER receiver. DONT_WAIT returns false on
    // backpressure (the Java analogue of
    // EAGAIN/EWOULDBLOCK/EHOSTUNREACH/ENOTCONN).
    private static void sendRouterStopToken(RouterSocket sender) {
        PerfStopToken.sendWithRetry(() -> {
            try (Message stop = PerfStopToken.newMessage()) {
                return sender.send(ROUTER1)
                    .message(stop)
                    .flags(SendFlags.DONT_WAIT)
                    .submit();
            }
        }, "router/router");
    }
}
