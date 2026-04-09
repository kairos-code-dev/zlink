/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class PerfRouterRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final RoutingId ROUTER1 = RoutingId.copyOf(
        "ROUTER1".getBytes(StandardCharsets.UTF_8));

    private PerfRouterRouter() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-router-router");
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch routed = new CountDownLatch(1);
        AtomicBoolean probePending = new AtomicBoolean(true);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        boolean sharedContext = "inproc".equals(config.transport());
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
            PerfUtil.configureServerTls(receiver, config.transport());
            PerfUtil.configureClientTls(sender, config.transport());
            receiver.setRoutingId(ROUTER1);
            sender.options().connectRoutingId(ROUTER1);
            receiver.bind(endpoint);
            sender.connect(endpoint);
            PerfUtil.waitForMonitorEvent(senderMonitor, READY_EVENTS, 1,
                readyTimeout, "router/router sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                readyTimeout, "router/router receiver ready");

            Thread receiverThread = new Thread(() -> {
                try (SocketPollSet pollSet = SocketPollSet.fromSockets(
                    List.of(receiver), PollEventType.POLLIN.getValue())) {
                    while (finished.getCount() > 0L) {
                        pollSet.poll(-1);
                        while (true) {
                            Optional<dev.kairoscode.zlink.Received> maybe = receiver.tryRecv();
                            if (maybe.isEmpty()) {
                                break;
                            }
                            try (var received = maybe.orElseThrow()) {
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
                                if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                    finished.countDown();
                                    return;
                                }
                                if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                    metrics.recordMicros(header.latencyMicros());
                                }
                            }
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-router-router-receiver");
            receiverThread.start();

            try (Message primer = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                sender.send(ROUTER1, List.of(primer));
            }
            PerfUtil.await(routed, "router/router self-check", Duration.ofSeconds(10));

            Thread traffic = SingleSendLoops.oneWaySend(
                () -> sender.send(ROUTER1, List.of(PerfUtil.payload(
                    config.size(), (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime()))),
                () -> sender.send(ROUTER1, List.of(PerfUtil.payload(
                    config.size(), (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime()))),
                config.durationSeconds(),
                metrics::startActiveWindow,
                failure,
                finished);
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
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                senderCtx.close();
            }
            receiverCtx.close();
        }
    }
}
