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
import java.util.concurrent.atomic.AtomicBoolean;

final class PerfMultiPubSub {
    private static final int READY_EVENTS =
        MonitorEventType.CONNECTION_READY.getValue();
    private static final int SUB_READY_EVENT =
        MonitorEventType.CONNECTION_READY.getValue();
    private static final String TOPIC = "perf.topic";

    private PerfMultiPubSub() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        if (!"recv".equalsIgnoreCase(config.recvMode())) {
            return PerfUtil.Result.unsupported("callback_not_allowed", config);
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
            sendStopBurst(pub, config.size(), config.clients());
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d, Double.NaN, Double.NaN);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch finishedClients = new CountDownLatch(config.clients());
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        MultiSendLoops.runClients(config.clients(), (index, warmup, duration) -> new Thread(() -> {
            CountDownLatch localDone = new CountDownLatch(1);
            AtomicBoolean localStopped = new AtomicBoolean(false);
            try (Context ctx = new Context();
                 SubSocket sub = new SubSocket(ctx);
                 var subMonitor = sub.monitorOpen(READY_EVENTS)) {
                sub.onSubscribe((routingId, topic, received) -> {
                    try (received) {
                        byte phase = PerfUtil.phase(received.firstPart());
                        if (phase == 1) {
                            if (localStopped.compareAndSet(false, true)) {
                                finishedClients.countDown();
                                localDone.countDown();
                            }
                            return;
                        }
                        if (phase == 0) {
                            metrics.recordMillis(PerfUtil.latencyMillis(received.firstPart()));
                        }
                    }
                });
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription(TOPIC);
                sub.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(subMonitor, SUB_READY_EVENT, 1,
                    Duration.ofSeconds(20), "pubsub subscriber ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startResourceWindow();
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "pubsub start", Duration.ofSeconds(10));
                PerfUtil.await(localDone, "pubsub multi", Duration.ofSeconds(
                    warmup + duration + 20L));
            }
        }, "multi-pubsub-client-" + index), config.warmupSeconds(), config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void send(PubSocket pub, int size, byte phase) {
        try (Message payload = PerfUtil.payload(size, phase, System.nanoTime())) {
            pub.publish(TOPIC, List.of(payload));
        }
    }

    private static void sendStopBurst(PubSocket pub, int size, int clients) {
        int burst = Math.max(3, clients * 3);
        for (int i = 0; i < burst; i++) {
            send(pub, size, (byte) 1);
        }
    }
}
