/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.perf.PerfControl;
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

final class PerfMultiSpot {
    private static final String TOPIC = "perf.topic";
    private static final String SERVICE_NAME = "perf.spot.service";
    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SPOT,
                 SERVICE_NAME);
             DealerSocket channelDealer = new DealerSocket(ctx);
             SpotNode node = new SpotNode(ctx);
             Spot publisher = node.createSpot()) {
            String registryPub = PerfUtil.endpoint(config.transport(),
                "multi-spot-registry-pub");
            String registryRouter = PerfUtil.endpoint(config.transport(),
                "multi-spot-registry-router");
            registry.bind(registryPub, registryRouter);
            discovery.connectRegistry(registryRouter);
            node.attachChannelDealerManual(SERVICE_NAME, channelDealer);
            node.attachDiscovery(discovery);
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            node.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "spot server");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    publisher.publish(SERVICE_NAME, TOPIC, List.of(m));
                }
            }
            for (int i = 0; i < Math.max(16, config.clients() * 8); i++) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                    publisher.publish(SERVICE_NAME, TOPIC, List.of(m));
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch ready = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        MultiSendLoops.runClients(config.clients(), (index, duration) ->
            new Thread(() -> runClientSlot(config, duration, ready, go,
                metrics, failure), "multi-spot-client-" + index),
            config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot client failed", failure.get());
        }
        return metrics.finishMulti(config);
    }

    private static void runClientSlot(PerfUtil.Config config, int duration,
                                      CountDownLatch ready,
                                      CountDownLatch go,
                                      PerfUtil.Metrics metrics,
                                      AtomicReference<Throwable> failure) {
        CountDownLatch finished = new CountDownLatch(1);
        try (Context ctx = PerfUtil.newContext(config);
             DealerSocket channelDealer = new DealerSocket(ctx);
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.createSpot()) {
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureClientTls(node, config.transport());
            node.attachChannelDealerManual(SERVICE_NAME, channelDealer);
            subscriber.setSubscription(TOPIC);
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
                            if (handleDelivery(received, metrics, config.size())) {
                                finished.countDown();
                                return;
                            }
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            });
            node.connectPeer(config.endpoint());
            settleAfterConnect();
            ready.countDown();
            if (ready.getCount() == 0L) {
                PerfControl.emitClientReady(config.size());
                PerfControl.awaitStart(config.size(), "spot client");
                metrics.startActiveWindow();
                go.countDown();
            }
            PerfUtil.await(go, "spot start", Duration.ofSeconds(10));
            PerfUtil.await(finished, "spot client recv",
                Duration.ofSeconds(duration + 20L));
        } catch (Throwable ex) {
            failure.compareAndSet(null, ex);
        }
    }

    private static boolean handleDelivery(dev.kairoscode.zlink.TopicMessage received,
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

    private static void settleAfterConnect() {
        int settleMs = PerfUtil.intEnv("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        try {
            Thread.sleep(settleMs);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot multi barrier interrupted", ex);
        }
    }
}
