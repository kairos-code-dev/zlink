/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfPair {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfPair() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pair");
        CountDownLatch finished = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        try (Context ctx = new Context();
             PairSocket receiver = new PairSocket(ctx);
             PairSocket sender = new PairSocket(ctx);
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
                Duration.ofSeconds(20), "pair sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                Duration.ofSeconds(20), "pair receiver ready");
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> sender.send(List.of(PerfUtil.payload(config.size(), (byte) 2, System.nanoTime()))),
                () -> sender.send(List.of(PerfUtil.payload(config.size(), (byte) 0, System.nanoTime()))),
                () -> sender.send(List.of(PerfUtil.payload(config.size(), (byte) 1, System.nanoTime()))),
                config.warmupSeconds(),
                config.durationSeconds(),
                metrics);
            traffic.start();
            PerfUtil.await(finished, "pair callback", Duration.ofSeconds(
                config.warmupSeconds() + config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "pair sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config.size(), config.durationSeconds());
        }
    }
}
