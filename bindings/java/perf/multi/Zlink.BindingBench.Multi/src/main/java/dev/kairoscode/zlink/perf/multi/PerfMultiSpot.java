/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

final class PerfMultiSpot {
    private static final long SPOT_FILTER_APPLIED = 1L << 13;
    private static final String TOPIC = "perf.topic";

    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot publisher = node.wrapHandle()) {
            configureNodeTlsServer(node, config.transport());
            node.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            long warmupEnd = System.nanoTime() + config.warmupSeconds() * 1_000_000_000L;
            while (System.nanoTime() < warmupEnd) {
                send(publisher, config.size(), (byte) 2);
            }
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                send(publisher, config.size(), (byte) 0);
            }
            for (int i = 0; i < config.clients(); i++) {
                send(publisher, config.size(), (byte) 1);
            }
            return new PerfUtil.Result("ok", "-", 0, 0, 0, 0, 0,
                Double.NaN, Double.NaN, "ms");
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch done = new CountDownLatch(config.clients());
        CountDownLatch ready = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        AtomicInteger stops = new AtomicInteger();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        MultiSendLoops.runClients(config.clients(), (index, warmup, duration) ->
            new Thread(() -> runClientSlot(config, index, warmup, duration, done,
                ready, go, stops, metrics), "multi-spot-client-" + index),
            config.warmupSeconds(), config.durationSeconds());
        return metrics.finishMulti(config.size(), config.durationSeconds());
    }

    private static void runClientSlot(PerfUtil.Config config, int index,
                                      int warmup, int duration,
                                      CountDownLatch done,
                                      CountDownLatch ready,
                                      CountDownLatch go,
                                      AtomicInteger stops,
                                      PerfUtil.Metrics metrics) {
        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = node.wrapHandle();
             ServiceMonitor monitor = subscriber.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            configureNodeTlsClient(node, config.transport());
            node.connectPeer(config.endpoint());
            subscriber.setSubscription(TOPIC);
            monitor.recv();
            ready.countDown();
            if (ready.getCount() == 0L) {
                metrics.startResourceWindow();
                PerfUtil.sendReadySignal(config.controlPort());
                go.countDown();
            }
            PerfUtil.await(go, "spot start", Duration.ofSeconds(10));
            if ("callback".equalsIgnoreCase(config.recvMode())) {
                subscriber.onSubscribe((routingId, topic, received) -> {
                    try (received) {
                        handleDelivery(config, done, stops, metrics, received.firstPart());
                    }
                });
                PerfUtil.await(done, "spot multi callback", Duration.ofSeconds(
                    warmup + duration + 20L));
                return;
            }
            while (done.getCount() > 0) {
                try (var received = subscriber.subscribe()) {
                    handleDelivery(config, done, stops, metrics,
                        received.firstPart());
                }
            }
        }
    }

    private static void handleDelivery(PerfUtil.Config config, CountDownLatch done,
                                       AtomicInteger stops,
                                       PerfUtil.Metrics metrics,
                                       Message payload) {
        byte phase = PerfUtil.phase(payload);
        if (phase == 1) {
            if (stops.incrementAndGet() <= config.clients()) {
                done.countDown();
            }
            return;
        }
        if (phase == 0) {
            metrics.recordMillis(PerfUtil.latencyMillis(payload));
        }
    }

    private static void send(Spot spot, int size, byte phase) {
        try (Message payload = PerfUtil.payload(size, phase, System.nanoTime())) {
            spot.publish(TOPIC, List.of(payload));
        }
    }

    private static void configureNodeTlsServer(SpotNode node, String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return;
        }
        node.setTlsServer(java.nio.file.Path.of("tests/certs/server.crt")
            .toAbsolutePath().toString(),
            java.nio.file.Path.of("tests/certs/server.key")
                .toAbsolutePath().toString(),
            false);
    }

    private static void configureNodeTlsClient(SpotNode node, String transport) {
        if (!"tls".equals(transport) && !"wss".equals(transport)) {
            return;
        }
        node.setTlsClient(java.nio.file.Path.of("tests/certs/ca.crt")
            .toAbsolutePath().toString(), "localhost", true);
    }
}
