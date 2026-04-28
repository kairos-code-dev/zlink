/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.perf.PerfControl;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiSpot {
    private static final String TOPIC = "bench";
    private static final String SERVICE_NAME = "bench-svc";

    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        String registryPubEndpoint = derivedEndpoint(config.endpoint(), 1);
        String registryRouterEndpoint = derivedEndpoint(config.endpoint(), 2);
        try (Context ctx = PerfUtil.newContext(config);
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT, SERVICE_NAME);
             SpotNode node = new SpotNode(ctx);
             Spot publisher = node.createSpot()) {
            registry.bind(registryPubEndpoint, registryRouterEndpoint);
            discovery.connectRegistry(registryRouterEndpoint);
            node.attachDiscovery(discovery);
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            node.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            sleepQuietly(2500);
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    publisher.publish(SERVICE_NAME, TOPIC, active, SendFlags.DONT_WAIT);
                }
            }
            long cooldownEnd = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (System.nanoTime() < cooldownEnd) {
                try (Message cooldown = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                    publisher.publish(SERVICE_NAME, TOPIC, cooldown, SendFlags.DONT_WAIT);
                }
                sleepQuietly(1);
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        String registryRouterEndpoint = normalizeClientEndpoint(
            derivedEndpoint(config.endpoint(), 2), config.transport());
        CountDownLatch ready = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        PerfMultiSendLoops.runClients(config.clients(), (index, duration) ->
            new Thread(() -> runClientSlot(config, registryRouterEndpoint, index, duration,
                ready, go, metrics, failure), "multi-spot-client-" + index),
            config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot client failed", failure.get());
        }
        return metrics.finishMulti(config);
    }

    private static void runClientSlot(PerfUtil.Config config,
                                      String registryRouterEndpoint,
                                      int index,
                                      int duration,
                                      CountDownLatch ready,
                                      CountDownLatch go,
                                      PerfUtil.Metrics metrics,
                                      AtomicReference<Throwable> failure) {
        try (Context ctx = PerfUtil.newContext(config);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT, SERVICE_NAME);
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.createSpot()) {
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureClientTls(node, config.transport());
            discovery.connectRegistry(registryRouterEndpoint);
            node.attachDiscovery(discovery);
            node.bind(PerfUtil.endpoint(config.transport(),
                "multi-spot-client-" + index));
            subscriber.setSubscription(TOPIC);
            settleReadyBarrier();
            ready.countDown();
            if (ready.getCount() == 0L) {
                PerfControl.emitClientReady(config.size());
                metrics.startActiveWindow();
                go.countDown();
            }
            PerfUtil.await(go, "spot start", Duration.ofSeconds(10));
            long finishDeadline = System.nanoTime()
                + Duration.ofSeconds(duration + 5L).toNanos();
            while (System.nanoTime() < finishDeadline) {
                try (TopicMessage received = subscriber.subscribe(RecvFlags.DONT_WAIT)) {
                    if (received == null) {
                        sleepQuietly(1);
                        continue;
                    }
                    if (handleDelivery(received, metrics, config.size())) {
                        return;
                    }
                }
            }
            throw new IllegalStateException("spot client cooldown timed out");
        } catch (Throwable ex) {
            failure.compareAndSet(null, ex);
        }
    }

    private static boolean handleDelivery(TopicMessage received,
                                          PerfUtil.Metrics metrics,
                                          int expectedSize) {
        if (received == null || received.parts().isEmpty()) {
            return false;
        }
        Message payload = received.firstPart();
        PerfUtil.Header header = PerfUtil.decodeHeader(payload, expectedSize);
        if (header == null) {
            return false;
        }
        if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
            return true;
        }
        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
            metrics.recordNanos(header.latencyNanos());
        }
        return false;
    }

    private static void settleReadyBarrier() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs > 0) {
            sleepQuietly(settleMs);
        }
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
}
