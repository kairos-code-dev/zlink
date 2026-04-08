/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.perf.PerfCallbackMetrics;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;

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
        PerfCallbackMetrics metrics = PerfUtil.callbackMetrics("single-router-router-metrics");
        AtomicBoolean probePending = new AtomicBoolean(true);
        boolean sharedContext = "inproc".equals(config.transport());
        Context receiverCtx = PerfUtil.newContext(config);
        Context senderCtx = sharedContext ? receiverCtx : PerfUtil.newContext(config);
        try (RouterSocket receiver = new RouterSocket(receiverCtx);
             RouterSocket sender = new RouterSocket(senderCtx);
             var receiverMonitor = receiver.monitorOpen(READY_EVENTS);
             var senderMonitor = sender.monitorOpen(READY_EVENTS)) {
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            PerfUtil.applyMonitorOptions(receiverMonitor, config);
            PerfUtil.applyMonitorOptions(senderMonitor, config);
            receiver.setRoutingId(ROUTER1);
            sender.options().connectRoutingId(ROUTER1);
            receiver.onReceive(received -> {
                try (received) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(received.firstPart(), config.size());
                    if (header == null) {
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_STOP) {
                        finished.countDown();
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_PROBE
                        && probePending.compareAndSet(true, false)) {
                        routed.countDown();
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordMicros(header.latencyMicros());
                    }
                }
            });
            PerfUtil.applySocketOptions(receiver, config);
            PerfUtil.applySocketOptions(sender, config);
            PerfUtil.configureServerTls(receiver, config.transport());
            PerfUtil.configureClientTls(sender, config.transport());
            receiver.bind(endpoint);
            sender.connect(endpoint);
            PerfUtil.waitForMonitorEvent(senderMonitor, READY_EVENTS, 1,
                readyTimeout, "router/router sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                readyTimeout, "router/router receiver ready");
            try (Message primer = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_PROBE, System.nanoTime())) {
                sender.send(ROUTER1, List.of(primer));
            }
            PerfUtil.await(routed, "router/router self-check", Duration.ofSeconds(10));
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) { sender.send(ROUTER1, List.of(m)); } },
                () -> { for (int i = 0; i < 16; i++) { try (Message m = PerfUtil.payload(config.size(), (byte) PerfUtil.PHASE_STOP, System.nanoTime())) { sender.send(ROUTER1, List.of(m)); } } },
                config.durationSeconds(),
                metrics::startActiveWindow);
            traffic.start();
            PerfUtil.await(finished, "router/router callback",
                Duration.ofSeconds(config.durationSeconds() + 30L));
            PerfUtil.join(traffic, "router/router sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                senderCtx.close();
            }
            receiverCtx.close();
        }
    }
}
