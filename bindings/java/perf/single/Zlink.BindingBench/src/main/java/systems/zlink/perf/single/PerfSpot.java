/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.Message;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.TopicMessage;
import systems.zlink.perf.PerfUtil;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.nio.charset.StandardCharsets;
import java.time.Duration;

final class PerfSpot {
    private static final String CHANNEL_NAME = "perf.spot.service";

    private PerfSpot() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String topic = "perf.topic." + System.nanoTime();
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

        try (var ctx = PerfUtil.newContext(config);
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, AutoConnectType.SPOT_MESH,
                 CHANNEL_NAME);
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
            PerfUtil.configureServerTls(registry, config.transport());
            PerfUtil.configureClientTls(discovery, config.transport());
            PerfUtil.applySpotOptions(publisherNode, config);
            PerfUtil.applySpotOptions(subscriberNode, config);
            PerfUtil.configureServerTls(publisherNode, config.transport());
            PerfUtil.configureClientTls(publisherNode, config.transport());
            PerfUtil.configureServerTls(subscriberNode, config.transport());
            PerfUtil.configureClientTls(subscriberNode, config.transport());
            publisherNode.setRoutingId(routingId("z-java-perf-spot-publisher"));
            subscriberNode.setRoutingId(routingId("a-java-perf-spot-subscriber"));
            publisher.setRoutingId(routingId("z-java-perf-spot-publisher-spot"));
            subscriber.setRoutingId(routingId("a-java-perf-spot-subscriber-spot"));
            registry.bind(registryPub, registryRouter);
            registry.setBroadcastInterval(Duration.ofMillis(50));
            discovery.connectRegistry(registryRouter);
            publisherNode.bind(publisherEndpoint);
            subscriberNode.bind(subscriberEndpoint);
            publisherNode.attachDiscovery(discovery);
            subscriberNode.attachDiscovery(discovery);
            subscriber.setSubscription(topic);

            waitForReadyProbe(publisher, subscriber, config, topic, metrics);
            settleAfterReady();

            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            metrics.startActiveWindow();
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    publisher.publish(topic)
                        .message(active)
                        .flags(SendFlags.DONT_WAIT)
                        .submit();
                }
                drainSubscriber(subscriber, config, metrics, activeEnd, true);
            }

            try (Message cooldown = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                publisher.publish(topic)
                    .message(cooldown)
                    .flags(SendFlags.DONT_WAIT)
                    .submit();
            }

            long idleDeadline = System.nanoTime()
                + Math.max(1, config.recvTimeoutMs()) * 1_000_000L;
            while (System.nanoTime() < idleDeadline) {
                if (!drainSubscriber(subscriber, config, metrics, activeEnd,
                        false)) {
                    sleepQuietly(Duration.ofMillis(1), "spot idle drain interrupted");
                }
            }

            ctx.shutdown();
            return metrics.finishSingle(config);
        }
    }

    private static void waitForReadyProbe(Spot publisher, Spot subscriber,
                                          PerfUtil.Config config, String topic,
                                          PerfUtil.Metrics metrics) {
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            try (Message probe = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                publisher.publish(topic)
                    .message(probe)
                    .flags(SendFlags.DONT_WAIT)
                    .submit();
            }
            if (drainSubscriber(subscriber, config, metrics, Long.MAX_VALUE,
                    false)) {
                return;
            }
            sleepQuietly(Duration.ofMillis(10), "spot local probe interrupted");
        }
        throw new IllegalStateException("spot local probe ready timeout");
    }

    private static boolean drainSubscriber(Spot subscriber,
                                           PerfUtil.Config config,
                                           PerfUtil.Metrics metrics,
                                           long activeEnd,
                                           boolean countActive) {
        boolean processed = false;
        while (true) {
            var maybe = PerfUtil.subscribeNoWait(subscriber);
            if (maybe.isEmpty()) {
                return processed;
            }
            try (TopicMessage subscribed = maybe.orElseThrow()) {
                PerfUtil.Header header = PerfUtil.decodeHeader(
                    subscribed.firstPart(), config.size());
                if (header == null) {
                    processed = true;
                    continue;
                }
                if (header.phase() == PerfUtil.PHASE_WARMUP) {
                    processed = true;
                    return true;
                }
                if (!countActive
                    || header.phase() != PerfUtil.PHASE_ACTIVE
                    || System.nanoTime() > activeEnd) {
                    processed = true;
                    continue;
                }
                metrics.recordNanos(header.latencyNanos());
                processed = true;
            }
        }
    }

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_SINGLE_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        sleepQuietly(Duration.ofMillis(settleMs), "spot settle interrupted");
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
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }

    private static RoutingId routingId(String value) {
        return RoutingId.fromBytes(value.getBytes(StandardCharsets.UTF_8));
    }
}
