/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfDealerDealer {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfDealerDealer() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-dealer-dealer");
        CountDownLatch finished = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        try (Context ctx = new Context();
             DealerSocket receiver = new DealerSocket(ctx);
             DealerSocket sender = new DealerSocket(ctx);
             var receiverMonitor = receiver.monitorOpen(READY_EVENTS);
             var senderMonitor = sender.monitorOpen(READY_EVENTS)) {
            receiver.onReceive(received -> {
                try (received) {
                    byte phase = PerfUtil.phase(received.firstPart());
                    if (phase == 1) {
                        finished.countDown();
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
                Duration.ofSeconds(20), "dealer/dealer sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                Duration.ofSeconds(20), "dealer/dealer receiver ready");
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> send(sender, config.size(), (byte) 2),
                () -> send(sender, config.size(), (byte) 0),
                () -> send(sender, config.size(), (byte) 1),
                config.warmupSeconds(),
                config.durationSeconds(),
                metrics);
            traffic.start();
            PerfUtil.await(finished, "dealer/dealer callback", Duration.ofSeconds(
                config.warmupSeconds() + config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "dealer/dealer sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config);
        }
    }

    private static void send(DealerSocket socket, int size, byte phase) {
        try (Message message = PerfUtil.payload(size, phase, System.nanoTime())) {
            socket.send(List.of(message));
        }
    }
}
