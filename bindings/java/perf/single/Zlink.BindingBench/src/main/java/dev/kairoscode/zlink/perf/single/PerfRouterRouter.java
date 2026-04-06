/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfRouterRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final RoutingId ROUTER1 = RoutingId.copyOf(
        "ROUTER1".getBytes(StandardCharsets.UTF_8));

    private PerfRouterRouter() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-router-router");
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch primed = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        boolean sharedContext = "inproc".equals(config.transport());
        Context receiverCtx = new Context();
        Context senderCtx = sharedContext ? receiverCtx : new Context();
        try (RouterSocket receiver = new RouterSocket(receiverCtx);
             RouterSocket sender = new RouterSocket(senderCtx);
             var receiverMonitor = receiver.monitorOpen(READY_EVENTS);
             var senderMonitor = sender.monitorOpen(READY_EVENTS)) {
            receiver.setRoutingId(ROUTER1);
            sender.options().connectRoutingId(ROUTER1);
            receiver.onReceive(received -> {
                try (received) {
                    byte phase = PerfUtil.phase(received.firstPart());
                    if (phase == 1) {
                        finished.countDown();
                        return;
                    }
                    if (phase == 2 && primed.getCount() > 0L) {
                        primed.countDown();
                        return;
                    }
                    if (phase == 0) {
                        metrics.recordMicros(PerfUtil.latencyMicros(received.firstPart()));
                    }
                }
            });
            PerfUtil.configureServerTls(receiver, config.transport());
            PerfUtil.configureClientTls(sender, config.transport());
            receiver.bind(endpoint);
            sender.connect(endpoint);
            PerfUtil.waitForMonitorEvent(senderMonitor, READY_EVENTS, 1,
                Duration.ofSeconds(20), "router/router sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                Duration.ofSeconds(20), "router/router receiver ready");
            try (Message primer = PerfUtil.payload(config.size(), (byte) 2, System.nanoTime())) {
                sender.send(ROUTER1, List.of(primer));
            }
            PerfUtil.await(primed, "router/router preflight", Duration.ofSeconds(10));
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) 2, System.nanoTime())) { sender.send(ROUTER1, List.of(m)); } },
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) 0, System.nanoTime())) { sender.send(ROUTER1, List.of(m)); } },
                () -> { for (int i = 0; i < 16; i++) { try (Message m = PerfUtil.payload(config.size(), (byte) 1, System.nanoTime())) { sender.send(ROUTER1, List.of(m)); } } },
                config.warmupSeconds(),
                config.durationSeconds(),
                metrics);
            traffic.start();
            PerfUtil.await(finished, "router/router callback", Duration.ofSeconds(
                config.warmupSeconds() + config.durationSeconds() + 30L));
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
