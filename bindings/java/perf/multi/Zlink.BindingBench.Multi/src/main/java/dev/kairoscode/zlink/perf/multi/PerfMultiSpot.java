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
import java.util.concurrent.atomic.AtomicReference;

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
            for (int i = 0; i < Math.max(16, config.clients() * 8); i++) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
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
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        MultiSendLoops.runClients(config.clients(), (index, duration) ->
            new Thread(() -> runClientSlot(config, duration, finishedClients,
                ready, go, metrics, failure), "multi-spot-client-" + index),
            config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("spot client failed", failure.get());
        }
        return metrics.finishMulti(config);
    }

    private static void runClientSlot(PerfUtil.Config config, int duration,
                                      CountDownLatch finishedClients,
                                      CountDownLatch ready,
                                      CountDownLatch go,
                                      PerfUtil.Metrics metrics,
                                      AtomicReference<Throwable> failure) {
        AtomicBoolean localStopped = new AtomicBoolean(false);
        try (Context ctx = PerfUtil.newContext(config);
             SpotNode node = new SpotNode(ctx);
             Spot subscriber = new Spot(node)) {
            PerfUtil.applySpotOptions(node, config);
            PerfUtil.configureClientTls(node, config.transport());
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
            while (finishedClients.getCount() > 0L) {
                try (var received = subscriber.subscribe()) {
                    handleDelivery(received, localStopped, finishedClients,
                        metrics, config.size());
                    if (localStopped.get()) {
                        return;
                    }
                }
            }
        } catch (Throwable ex) {
            failure.compareAndSet(null, ex);
        }
    }

    private static void handleDelivery(dev.kairoscode.zlink.TopicMessage received,
                                       AtomicBoolean localStopped,
                                       CountDownLatch finishedClients,
                                       PerfUtil.Metrics metrics,
                                       int expectedSize) {
        if (received == null || received.parts().isEmpty()) {
            return;
        }
        Message payload = received.firstPart();
        PerfUtil.Header header = PerfUtil.decodeHeader(payload, expectedSize);
        if (header == null) {
            return;
        }
        if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
            if (localStopped.compareAndSet(false, true)) {
                finishedClients.countDown();
            }
            return;
        }
        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
            metrics.recordMicros(header.latencyMicros());
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
