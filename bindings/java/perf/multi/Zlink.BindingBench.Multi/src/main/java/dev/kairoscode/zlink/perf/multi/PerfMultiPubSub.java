/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;

final class PerfMultiPubSub {
    private static final int READY_EVENTS =
        MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue()
            | MonitorEventType.PUB_DELIVERY_READY_CHANGED.getValue();

    private PerfMultiPubSub() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        if (!"recv".equalsIgnoreCase(config.recvMode())) {
            return new PerfUtil.Result("unsupported", "callback_not_allowed", 0, 0,
                0, 0, 0, Double.NaN, Double.NaN, "ms");
        }
        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             var monitor = pub.monitorOpen(READY_EVENTS)) {
            PerfUtil.configureServerTls(pub, config.transport());
            pub.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            monitor.recv();
            long warmupEnd = System.nanoTime() + config.warmupSeconds() * 1_000_000_000L;
            while (System.nanoTime() < warmupEnd) {
                send(pub, config.size(), (byte) 2);
            }
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                send(pub, config.size(), (byte) 0);
            }
            send(pub, config.size(), (byte) 1);
            return new PerfUtil.Result("ok", "-", 0, 0, 0, 0, 0, Double.NaN, Double.NaN, "ms");
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch done = new CountDownLatch(config.clients());
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        AtomicInteger stops = new AtomicInteger();
        MultiSendLoops.runClients(config.clients(), (index, warmup, duration) -> new Thread(() -> {
            try (Context ctx = new Context();
                 SubSocket sub = new SubSocket(ctx)) {
                sub.onSubscribe((routingId, topic, received) -> {
                    try (received) {
                        byte phase = PerfUtil.phase(received.firstPart());
                        if (phase == 1) {
                            if (stops.incrementAndGet() <= config.clients()) {
                                done.countDown();
                            }
                            return;
                        }
                        if (phase == 0) {
                            metrics.recordMillis(PerfUtil.latencyMillis(received.firstPart()));
                        }
                    }
                });
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription("perf.topic");
                sub.connect(config.endpoint());
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startResourceWindow();
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "pubsub start", Duration.ofSeconds(10));
                PerfUtil.await(done, "pubsub multi", Duration.ofSeconds(
                    warmup + duration + 20L));
            }
        }, "multi-pubsub-client-" + index), config.warmupSeconds(), config.durationSeconds());
        return metrics.finishMulti(config.size(), config.durationSeconds());
    }

    private static void send(PubSocket pub, int size, byte phase) {
        try (Message payload = PerfUtil.payload(size, phase, System.nanoTime())) {
            pub.publish("perf.topic", List.of(payload));
        }
    }
}
