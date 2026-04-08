/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.perf.PerfCallbackMetrics;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfSpot {
    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String topic = "perf.topic." + System.nanoTime();
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch ready = new CountDownLatch(1);
        PerfCallbackMetrics metrics = PerfUtil.callbackMetrics("single-spot-metrics");
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot spot = new Spot(node)) {
            PerfUtil.applySpotOptions(node, config);
            spot.onSubscribe((routingId, recvTopic, received) -> {
                try (received) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(received.firstPart(), config.size());
                    if (header == null) {
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_STOP) {
                        finished.countDown();
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_PROBE && ready.getCount() > 0L) {
                        ready.countDown();
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordMicros(header.latencyMicros());
                    }
                }
            });
            spot.setSubscription(topic);
            try (Message probe = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_PROBE, System.nanoTime())) {
                spot.publish(topic, List.of(probe));
            }
            PerfUtil.await(ready, "spot local probe ready", Duration.ofSeconds(10));
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) { spot.publish(topic, List.of(m)); } },
                () -> { try (Message m = PerfUtil.payload(config.size(), (byte) PerfUtil.PHASE_STOP, System.nanoTime())) { spot.publish(topic, List.of(m)); } },
                config.durationSeconds(),
                metrics::startActiveWindow);
            traffic.start();
            PerfUtil.await(finished, "spot callback",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "spot sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config);
        }
    }
}
