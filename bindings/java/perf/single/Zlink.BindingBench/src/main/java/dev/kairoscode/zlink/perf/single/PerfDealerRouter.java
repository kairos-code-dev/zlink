/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.perf.PerfCallbackMetrics;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfDealerRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfDealerRouter() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-dealer-router");
        CountDownLatch finished = new CountDownLatch(1);
        PerfCallbackMetrics metrics = PerfUtil.callbackMetrics("single-dealer-router-metrics");
        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket receiver = new RouterSocket(ctx);
             DealerSocket sender = new DealerSocket(ctx);
             var receiverMonitor = receiver.monitorOpen(READY_EVENTS);
             var senderMonitor = sender.monitorOpen(READY_EVENTS)) {
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            PerfUtil.applyMonitorOptions(receiverMonitor, config);
            PerfUtil.applyMonitorOptions(senderMonitor, config);
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
                readyTimeout, "dealer/router sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                readyTimeout, "dealer/router receiver ready");
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) { sender.send(List.of(m)); } },
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) PerfUtil.PHASE_STOP, System.nanoTime())) { sender.send(List.of(m)); } },
                config.durationSeconds(),
                metrics::startActiveWindow);
            traffic.start();
            PerfUtil.await(finished, "dealer/router callback",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "dealer/router sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config);
        }
    }
}
