/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.perf.PerfCallbackMetrics;
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
    private static final Duration STABILIZATION = Duration.ofSeconds(1);
    private static final Duration CONTROL_SETTLE = Duration.ofMillis(25);

    private PerfMultiSpot() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
            Spot publisher = new Spot(node)) {
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureServerTls(node, config.transport());
            node.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            sleepQuietly(CONTROL_SETTLE);
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    publisher.publish(TOPIC, List.of(m));
                }
            }
            int stopBurst = Math.max(16, config.clients() * 8);
            for (int i = 0; i < stopBurst; i++) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_STOP, System.nanoTime())) {
                    publisher.publish(TOPIC, List.of(m));
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch finishedClients = new CountDownLatch(config.clients());
        CountDownLatch ready = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfCallbackMetrics metrics = PerfUtil.callbackMetrics("multi-spot-metrics");
        MultiSendLoops.runClients(config.clients(), (index, duration) ->
            new Thread(() -> runClientSlot(config, index, duration,
                finishedClients, ready, go, metrics), "multi-spot-client-" + index),
            config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void runClientSlot(PerfUtil.Config config, int index,
                                      int duration,
                                      CountDownLatch finishedClients,
                                      CountDownLatch ready,
                                      CountDownLatch go,
                                      PerfCallbackMetrics metrics) {
        CountDownLatch localDone = new CountDownLatch(1);
        AtomicBoolean localStopped = new AtomicBoolean(false);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = new Spot(node);
        ) {
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureClientTls(node, config.transport());
            if ("callback".equalsIgnoreCase(config.recvMode())) {
                subscriber.onSubscribe((routingId, topic, received) -> {
                    if (received == null) {
                        return;
                    }
                    try (received) {
                        handleDelivery(localDone, localStopped, finishedClients,
                            metrics, config.size(), received);
                    }
                });
            }
            node.connectPeer(config.endpoint());
            subscriber.setSubscription(TOPIC);
            sleepQuietly(STABILIZATION);
            ready.countDown();
            if (ready.getCount() == 0L) {
                metrics.startActiveWindow();
                PerfUtil.sendReadySignal(config.controlPort());
                go.countDown();
            }
            PerfUtil.await(go, "spot start", Duration.ofSeconds(10));
            if ("callback".equalsIgnoreCase(config.recvMode())) {
                waitForStopOrDeadline(localDone, Duration.ofSeconds(
                    duration + 10L));
                return;
            }
            while (localDone.getCount() > 0L) {
                try (var received = subscriber.subscribe()) {
                    handleDelivery(localDone, localStopped, finishedClients,
                        metrics, config.size(), received);
                }
            }
        }
    }

    private static void handleDelivery(CountDownLatch localDone,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfCallbackMetrics metrics,
                                       int expectedSize,
                                       dev.kairoscode.zlink.Received received) {
        if (received == null || received.parts().isEmpty()) {
            return;
        }
        handleDelivery(localDone, localStopped, finishedClients, metrics,
            expectedSize, received.firstPart());
    }

    private static void handleDelivery(CountDownLatch localDone,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfCallbackMetrics metrics,
                                       int expectedSize,
                                       dev.kairoscode.zlink.TopicMessage received) {
        if (received == null || received.parts().isEmpty()) {
            return;
        }
        handleDelivery(localDone, localStopped, finishedClients, metrics,
            expectedSize, received.firstPart());
    }

    private static void handleDelivery(CountDownLatch localDone,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfCallbackMetrics metrics,
                                       int expectedSize,
                                       Message payload) {
        PerfUtil.Header header = PerfUtil.decodeHeader(payload, expectedSize);
        if (header == null) {
            return;
        }
        if (header.phase() == PerfUtil.PHASE_STOP) {
            if (localStopped.compareAndSet(false, true)) {
                finishedClients.countDown();
                localDone.countDown();
            }
            return;
        }
        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
            metrics.recordMicros(header.latencyMicros());
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

    private static void sleepQuietly(Duration duration) {
        try {
            Thread.sleep(duration.toMillis());
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("spot multi barrier interrupted", ex);
        }
    }

}
