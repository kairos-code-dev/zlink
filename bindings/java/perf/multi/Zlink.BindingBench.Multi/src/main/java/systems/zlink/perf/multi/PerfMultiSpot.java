/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RecvFlags;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.TopicMessage;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNode;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

final class PerfMultiSpot {
    private static final String TOPIC = "bench";
    private static final String CHANNEL_NAME = "bench-svc";
    private static final int MAX_DRAIN_PER_SPOT = 1024;

    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        String registryPubEndpoint = derivedEndpoint(config.endpoint(), 1);
        String registryRouterEndpoint = derivedEndpoint(config.endpoint(), 2);
        try (Context ctx = PerfUtil.newContext(config);
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, AutoConnectType.SPOT_MESH, CHANNEL_NAME);
             SpotNode node = new SpotNode(ctx);
             Spot publisher = node.createSpot()) {
            node.setRoutingId(routingId("z-java-multi-spot-server"));
            publisher.setRoutingId(routingId("z-java-multi-spot-server-spot"));
            PerfUtil.configureServerTls(registry, config.transport());
            PerfUtil.configureClientTls(discovery, config.transport());
            registry.bind(registryPubEndpoint, registryRouterEndpoint);
            registry.setBroadcastInterval(Duration.ofMillis(50));
            discovery.connectRegistry(registryRouterEndpoint);
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            node.bind(config.endpoint());
            node.attachDiscovery(discovery);
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "spot server");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    publisher.publish(TOPIC)
                        .message(active)
                        .flags(SendFlags.DONT_WAIT)
                        .submit();
                }
            }
            // PERF_MULTI_TEST_POLICY § 1.3.1: signal phase end with one
            // wire-level stop token. Brief retry burst guards against transient
            // best-effort backpressure on the per-spot publish path.
            long stopBurstEnd = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            int sent = 0;
            while (sent < 3 && System.nanoTime() < stopBurstEnd) {
                try (Message stop = PerfStopToken.newMessage()) {
                    publisher.publish(TOPIC)
                        .message(stop)
                        .flags(SendFlags.DONT_WAIT)
                        .submit();
                    sent++;
                }
                sleepQuietly(1);
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        String registryRouterEndpoint = normalizeClientEndpoint(
            derivedEndpoint(config.endpoint(), 2), config.transport());
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        try (Context ctx = PerfUtil.newContext(config);
             Discovery discovery = new Discovery(ctx, AutoConnectType.SPOT_MESH, CHANNEL_NAME);
             SpotNode node = new SpotNode(ctx)) {
            node.setRoutingId(routingId("a-java-multi-spot-client"));
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureClientTls(node, config.transport());
            PerfUtil.configureClientTls(discovery, config.transport());

            List<Spot> subscribers = new ArrayList<>(config.clients());
            try {
                for (int i = 0; i < config.clients(); i++) {
                    Spot subscriber = node.createSpot();
                    subscriber.setRoutingId(routingId(
                        "a-java-multi-spot-client-spot-" + i));
                    subscribers.add(subscriber);
                }
                discovery.connectRegistry(registryRouterEndpoint);
                node.bind(PerfUtil.endpoint(config.transport(),
                    "multi-spot-client"));
                node.attachDiscovery(discovery);
                for (Spot subscriber : subscribers) {
                    subscriber.setSubscription(TOPIC);
                }
                waitForPeerConnected(node, config.connectReadyTimeoutMs());
                runClientSubscribers(config, subscribers, metrics);
                return metrics.finishMulti(config);
            } finally {
                for (Spot subscriber : subscribers) {
                    try {
                        subscriber.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
    }

    private static void runClientSubscribers(PerfUtil.Config config,
                                             List<Spot> subscribers,
                                             PerfUtil.Metrics metrics) {
        boolean[] cooldownSeen = new boolean[subscribers.size()];
        try {
            settleReadyBarrier();
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "spot client");
            metrics.startActiveWindow();
            long activeEnd = System.nanoTime()
                + Duration.ofSeconds(config.durationSeconds()).toNanos();
            long finishDeadline = activeEnd
                + Duration.ofMillis(postPhaseSettleMs()).toNanos();
            while (System.nanoTime() < finishDeadline) {
                boolean progressed = false;
                for (int i = 0; i < subscribers.size(); i++) {
                    if (cooldownSeen[i]) {
                        continue;
                    }
                    for (int drained = 0; drained < MAX_DRAIN_PER_SPOT; drained++) {
                        try (TopicMessage received =
                                 subscribers.get(i).subscribe(RecvFlags.DONT_WAIT)) {
                            if (received == null) {
                                break;
                            }
                            progressed = true;
                            int phase = handleDelivery(received, metrics,
                                config.size(), activeEnd);
                            if (phase == PerfUtil.PHASE_COOLDOWN) {
                                cooldownSeen[i] = true;
                                break;
                            }
                        }
                    }
                }
                if (allCooldownSeen(cooldownSeen)) {
                    return;
                }
            }
            return;
        } catch (Throwable ex) {
            throw new IllegalStateException("spot client failed", ex);
        }
    }

    private static boolean allCooldownSeen(boolean[] cooldownSeen) {
        if (cooldownSeen.length == 0) {
            return false;
        }
        for (boolean seen : cooldownSeen) {
            if (!seen) {
                return false;
            }
        }
        return true;
    }

    private static int countCooldownSeen(boolean[] cooldownSeen) {
        int count = 0;
        for (boolean seen : cooldownSeen) {
            if (seen) {
                count++;
            }
        }
        return count;
    }

    private static int handleDelivery(TopicMessage received,
                                      PerfUtil.Metrics metrics,
                                      int expectedSize,
                                      long activeEnd) {
        if (received == null || received.parts().isEmpty()) {
            return PerfUtil.PHASE_UNKNOWN;
        }
        Message payload = received.firstPart();
        // PERF_MULTI_TEST_POLICY § 1.3.1: per-spot stop token signals phase end.
        // Reuse the COOLDOWN return code so the existing per-spot bookkeeping
        // (cooldownSeen[]) stays unchanged.
        if (PerfStopToken.isStopTokenMessage(payload)) {
            return PerfUtil.PHASE_COOLDOWN;
        }
        PerfUtil.Header header = PerfUtil.decodeHeader(payload, expectedSize);
        if (header == null) {
            return PerfUtil.PHASE_UNKNOWN;
        }
        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
            if (System.nanoTime() <= activeEnd) {
                metrics.recordNanos(header.latencyNanos());
            }
            return PerfUtil.PHASE_ACTIVE;
        }
        return header.phase();
    }

    private static int postPhaseSettleMs() {
        return PerfUtil.intEnv("PERF_MULTI_SPOT_POST_PHASE_SETTLE_MS", 0);
    }

    private static void settleReadyBarrier() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs > 0) {
            sleepQuietly(settleMs);
        }
        int controlSettleMs = PerfUtil.intEnv(
            "PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25);
        if (controlSettleMs > 0) {
            sleepQuietly(controlSettleMs);
        }
    }

    private static void waitForPeerConnected(SpotNode node, int timeoutMs) {
        long deadline = System.nanoTime()
            + Duration.ofMillis(Math.max(1, timeoutMs)).toNanos();
        while (System.nanoTime() < deadline) {
            if (node.statusSnapshot().connectedPeerCount() > 0) {
                return;
            }
            sleepQuietly(10);
        }
        throw new IllegalStateException("spot client peer connect timed out");
    }

    private static void sleepQuietly(int millis) {
        sleepQuietly((long) millis);
    }

    private static void sleepQuietly(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot ready barrier interrupted", ex);
        }
    }

    private static String normalizeClientEndpoint(String endpoint, String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return endpoint;
        }
        return endpoint.replace("://127.0.0.1:", "://localhost:");
    }

    private static String derivedEndpoint(String endpoint, int portOffset) {
        int schemeSep = endpoint.indexOf("://");
        int colon = endpoint.lastIndexOf(':');
        if (schemeSep <= 0 || colon <= schemeSep + 2 || colon == endpoint.length() - 1) {
            throw new IllegalArgumentException("cannot derive endpoint from: " + endpoint);
        }
        int port = Integer.parseInt(endpoint.substring(colon + 1));
        return endpoint.substring(0, colon + 1) + (port + portOffset);
    }

    private static RoutingId routingId(String value) {
        return RoutingId.fromBytes(value.getBytes(StandardCharsets.UTF_8));
    }
}
