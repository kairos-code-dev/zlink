/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfSpot {
    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String topic = "perf.topic." + System.nanoTime();
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch ready = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        String endpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot"),
            config.transport());
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode pubNode = new SpotNode(ctx);
             SpotNode subNode = new SpotNode(ctx);
             Spot publisher = pubNode.createSpot();
             Spot subscriber = subNode.createSpot()) {
            PerfUtil.applySpotOptions(pubNode, config);
            PerfUtil.applySpotOptions(subNode, config);
            PerfUtil.configureServerTls(pubNode, config.transport());
            PerfUtil.configureClientTls(subNode, config.transport());
            pubNode.bind(endpoint);
            subNode.connectPeer(endpoint);
            subscriber.setSubscription(topic);
            sleepQuietly(Duration.ofMillis(250), "spot settle interrupted");

            Thread receiverThread = new Thread(() -> {
                try {
                    while (finished.getCount() > 0L) {
                        try (var received = subscriber.subscribe()) {
                            PerfUtil.Header header = PerfUtil.decodeHeader(
                                received.firstPart(), config.size());
                            if (header == null) {
                                continue;
                            }
                            if (header.phase() == PerfUtil.PHASE_WARMUP
                                && ready.getCount() > 0L) {
                                ready.countDown();
                                continue;
                            }
                            if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                finished.countDown();
                                return;
                            }
                            if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                metrics.recordNanos(header.latencyNanos());
                            }
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-spot-receiver");
            receiverThread.start();

            long readyDeadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
            while (ready.getCount() > 0L && System.nanoTime() < readyDeadline) {
                try (Message probe = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                    publisher.publish(topic, List.of(probe));
                }
                sleepQuietly(Duration.ofMillis(10), "spot probe interrupted");
            }
            PerfUtil.await(ready, "spot local probe ready", Duration.ofSeconds(10));

            Thread traffic = new Thread(() -> {
                metrics.startActiveWindow();
                long activeEnd = System.nanoTime()
                    + config.durationSeconds() * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        publisher.publish(topic, List.of(m));
                    }
                }
                for (int i = 0; i < 8; i++) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        publisher.publish(topic, List.of(m));
                    }
                    sleepQuietly(Duration.ofMillis(2), "spot cooldown interrupted");
                }
            }, "single-spot-sender");
            traffic.start();
            PerfUtil.await(finished, "spot receiver",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "spot sender", Duration.ofSeconds(10));
            PerfUtil.join(receiverThread, "spot receiver thread", Duration.ofSeconds(10));
            if (failure.get() != null) {
                throw new IllegalStateException("spot receiver failed", failure.get());
            }
            return metrics.finishSingle(config);
        }
    }

    private static void sleepQuietly(Duration duration, String label) {
        try {
            Thread.sleep(duration.toMillis());
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label, ex);
        }
    }

    private static String normalizeSpotEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }
}
