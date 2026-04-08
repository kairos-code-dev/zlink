/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.perf.PerfCallbackMetrics;
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
        try (Context ctx = PerfUtil.newContext(config);
             PubSocket pub = new PubSocket(ctx);
             var monitor = pub.monitorOpen(READY_EVENTS)) {
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.configureServerTls(pub, config.transport());
            pub.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            monitor.recv();
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    pub.publish(TOPIC, List.of(m));
                }
            }
            int burst = Math.max(3, config.clients() * 3);
            for (int i = 0; i < burst; i++) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_STOP, System.nanoTime())) {
                    pub.publish(TOPIC, List.of(m));
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch finishedClients = new CountDownLatch(config.clients());
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfCallbackMetrics metrics = PerfUtil.callbackMetrics("multi-pubsub-metrics");
        MultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
            CountDownLatch localDone = new CountDownLatch(1);
            AtomicBoolean localStopped = new AtomicBoolean(false);
            try (Context ctx = PerfUtil.newContext(config);
                 SubSocket sub = new SubSocket(ctx);
                 var subMonitor = sub.monitorOpen(READY_EVENTS)) {
                PerfUtil.applyMonitorOptions(subMonitor, config);
                PerfUtil.applySocketOptions(sub, config);
                sub.onSubscribe((routingId, topic, received) -> {
                    try (received) {
                        PerfUtil.Header header = PerfUtil.decodeHeader(received.firstPart(), config.size());
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
                });
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription(TOPIC);
                sub.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(subMonitor, SUB_READY_EVENT, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()), "pubsub subscriber ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startActiveWindow();
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "pubsub start", Duration.ofSeconds(10));
                PerfUtil.await(localDone, "pubsub multi", Duration.ofSeconds(
                    duration + 20L));
            }
        }, "multi-pubsub-client-" + index), config.durationSeconds());
        return metrics.finishMulti(config);
    }
}
