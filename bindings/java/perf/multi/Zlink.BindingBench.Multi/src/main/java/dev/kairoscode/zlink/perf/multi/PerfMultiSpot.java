/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.perf.PerfUtil;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.TimeUnit;

final class PerfMultiSpot {
    private static final String TOPIC = "perf.topic";

    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot publisher = new Spot(node)) {
            configureNodeTlsServer(node, config.transport());
            node.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            long warmupEnd = System.nanoTime() + config.warmupSeconds() * 1_000_000_000L;
            while (System.nanoTime() < warmupEnd) {
                try (Message m = PerfUtil.payload(config.size(), (byte) 2, System.nanoTime())) {
                    publisher.publish(TOPIC, List.of(m));
                }
            }
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message m = PerfUtil.payload(config.size(), (byte) 0, System.nanoTime())) {
                    publisher.publish(TOPIC, List.of(m));
                }
            }
            for (int i = 0; i < config.clients(); i++) {
                try (Message m = PerfUtil.payload(config.size(), (byte) 1, System.nanoTime())) {
                    publisher.publish(TOPIC, List.of(m));
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d, Double.NaN, Double.NaN);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch finishedClients = new CountDownLatch(config.clients());
        CountDownLatch ready = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        MultiSendLoops.runClients(config.clients(), (index, warmup, duration) ->
            new Thread(() -> runClientSlot(config, index, warmup, duration,
                finishedClients, ready, go, metrics), "multi-spot-client-" + index),
            config.warmupSeconds(), config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void runClientSlot(PerfUtil.Config config, int index,
                                      int warmup, int duration,
                                      CountDownLatch finishedClients,
                                      CountDownLatch ready,
                                      CountDownLatch go,
                                      PerfUtil.Metrics metrics) {
        CountDownLatch localDone = new CountDownLatch(1);
        AtomicBoolean localStopped = new AtomicBoolean(false);
        try (Context ctx = new Context();
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = new Spot(node);
        ) {
            configureNodeTlsClient(node, config.transport());
            if ("callback".equalsIgnoreCase(config.recvMode())) {
                subscriber.onSubscribe((routingId, topic, received) -> {
                    if (received == null) {
                        return;
                    }
                    try (received) {
                        handleDelivery(localDone, localStopped, finishedClients,
                            metrics, received);
                    }
                });
            }
            node.connectPeer(config.endpoint());
            subscriber.setSubscription(TOPIC);
            ready.countDown();
            if (ready.getCount() == 0L) {
                metrics.startResourceWindow();
                PerfUtil.sendReadySignal(config.controlPort());
                go.countDown();
            }
            PerfUtil.await(go, "spot start", Duration.ofSeconds(10));
            if ("callback".equalsIgnoreCase(config.recvMode())) {
                waitForStopOrDeadline(localDone, Duration.ofSeconds(
                    warmup + duration + 10L));
                return;
            }
            while (localDone.getCount() > 0L) {
                try (var received = subscriber.subscribe()) {
                    handleDelivery(localDone, localStopped, finishedClients,
                        metrics, received);
                }
            }
        }
    }

    private static void handleDelivery(CountDownLatch localDone,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfUtil.Metrics metrics,
                                       dev.kairoscode.zlink.Received received) {
        if (received == null || received.parts().isEmpty()) {
            return;
        }
        handleDelivery(localDone, localStopped, finishedClients, metrics,
            received.firstPart());
    }

    private static void handleDelivery(CountDownLatch localDone,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfUtil.Metrics metrics,
                                       dev.kairoscode.zlink.TopicMessage received) {
        if (received == null || received.parts().isEmpty()) {
            return;
        }
        handleDelivery(localDone, localStopped, finishedClients, metrics,
            received.firstPart());
    }

    private static void handleDelivery(CountDownLatch localDone,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfUtil.Metrics metrics,
                                       Message payload) {
        byte phase = PerfUtil.phase(payload);
        if (phase == 1) {
            if (localStopped.compareAndSet(false, true)) {
                finishedClients.countDown();
                localDone.countDown();
            }
            return;
        }
        if (phase == 0) {
            metrics.recordMillis(PerfUtil.latencyMillis(payload));
        }
    }

    private static void waitForStopOrDeadline(CountDownLatch localDone,
                                              Duration timeout) {
        try {
            localDone.await(timeout.toNanos(), TimeUnit.NANOSECONDS);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot multi callback interrupted", ex);
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
