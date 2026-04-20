/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfSpot {
    private static final String SERVICE_NAME = "perf.spot.service";

    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String topic = "perf.topic." + System.nanoTime();
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch ready = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        String registryPub = PerfUtil.endpoint(config.transport(),
            "single-spot-registry-pub");
        String registryRouter = PerfUtil.endpoint(config.transport(),
            "single-spot-registry-router");
        String publisherEndpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot-pub"),
            config.transport());
        String subscriberEndpoint = normalizeSpotEndpoint(
            PerfUtil.endpoint(config.transport(), "single-spot-sub"),
            config.transport());
        try (Context ctx = PerfUtil.newContext(config);
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT,
                 SERVICE_NAME);
             SpotNode pubNode = new SpotNode(ctx);
             SpotNode subNode = new SpotNode(ctx);
             Spot publisher = pubNode.createSpot();
             Spot subscriber = subNode.createSpot()) {
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            pubNode.attachDiscovery(discovery);
            subNode.attachDiscovery(discovery);
            PerfUtil.applySpotOptions(pubNode, config);
            PerfUtil.applySpotOptions(subNode, config);
            PerfUtil.configureServerTls(pubNode, config.transport());
            PerfUtil.configureClientTls(subNode, config.transport());
            pubNode.bind(publisherEndpoint);
            subNode.bind(subscriberEndpoint);
            subscriber.setSubscription(topic);
            subscriber.onDispatchEvent(event -> {
                if (event != SpotDispatchEvent.SUBSCRIBE_READABLE) {
                    return;
                }
                try {
                    while (true) {
                        var maybe = PerfUtil.subscribeNoWait(subscriber);
                        if (maybe.isEmpty()) {
                            return;
                        }
                        try (var received = maybe.orElseThrow()) {
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
                                continue;
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
            });

            long readyDeadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
            while (ready.getCount() > 0L && System.nanoTime() < readyDeadline) {
                try (Message probe = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                    publisher.publish(SERVICE_NAME, topic, List.of(probe));
                }
                Thread.onSpinWait();
            }
            PerfUtil.await(ready, "spot local probe ready", Duration.ofSeconds(10));
            settleAfterReady();

            Thread traffic = new Thread(() -> {
                try {
                    metrics.startActiveWindow();
                    long activeEnd = System.nanoTime()
                        + config.durationSeconds() * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        try (Message m = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                            publisher.publish(SERVICE_NAME, topic, List.of(m));
                        }
                    }
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        publisher.publish(SERVICE_NAME, topic, List.of(m));
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-spot-sender");
            traffic.start();
            PerfUtil.await(finished, "spot receiver",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "spot sender", Duration.ofSeconds(10));
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

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_SINGLE_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        sleepQuietly(Duration.ofMillis(settleMs), "spot settle interrupted");
    }

    private static String normalizeSpotEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }
}
