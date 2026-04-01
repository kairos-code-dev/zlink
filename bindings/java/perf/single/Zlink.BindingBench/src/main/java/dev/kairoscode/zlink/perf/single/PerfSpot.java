/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfSpot {
    private static final long SPOT_FILTER_APPLIED = 1L << 13;

    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String topic = "perf.topic." + System.nanoTime();
        CountDownLatch finished = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot spot = new Spot(node);
             ServiceMonitor monitor = spot.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            spot.onSubscribe((routingId, recvTopic, received) -> {
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
            spot.setSubscription(topic);
            monitor.recv();
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> send(spot, topic, config.size(), (byte) 2),
                () -> send(spot, topic, config.size(), (byte) 0),
                () -> send(spot, topic, config.size(), (byte) 1),
                config.warmupSeconds(),
                config.durationSeconds(),
                metrics);
            traffic.start();
            PerfUtil.await(finished, "spot callback", Duration.ofSeconds(
                config.warmupSeconds() + config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "spot sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config);
        }
    }

    private static void send(Spot spot, String topic, int size, byte phase) {
        try (Message message = PerfUtil.payload(size, phase, System.nanoTime())) {
            spot.publish(topic, List.of(message));
        }
    }
}
