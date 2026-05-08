/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.PollEventFlag;
import systems.zlink.PubSocket;
import systems.zlink.RecvFlags;
import systems.zlink.SubSocket;
import systems.zlink.TopicMessage;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

final class PerfPubSub {
    private static final MonitorEventType READY_EVENT = MonitorEventType.CONNECTION_READY;
    private static final String TOPIC = "perf.topic";

    private PerfPubSub() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pubsub");
        AtomicBoolean idleDrain = new AtomicBoolean(false);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        boolean sharedContext = "inproc".equals(config.transport());
        Context pubCtx = PerfUtil.newContext(config);
        Context subCtx = sharedContext ? pubCtx : PerfUtil.newContext(config);
        try (PubSocket pub = new PubSocket(pubCtx);
             SubSocket sub = new SubSocket(subCtx)) {
            var pubMonitor = pub.monitorOpen(MonitorEventType.CONNECTION_READY);
            var subMonitor = sub.monitorOpen(MonitorEventType.CONNECTION_READY);
            try {
            PerfUtil.applyMonitorOptions(pubMonitor, config);
            PerfUtil.applyMonitorOptions(subMonitor, config);
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.applySocketOptions(sub, config);
            pub.options().noDrop(true);
            PerfUtil.configureServerTls(pub, config.transport());
            PerfUtil.configureClientTls(sub, config.transport());
            pub.bind(endpoint);
            sub.setSubscription(TOPIC);
            sub.connect(endpoint);
            PerfUtil.waitForMonitorEvent(pubMonitor, READY_EVENT, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "pubsub publisher ready");
            PerfUtil.waitForMonitorEvent(subMonitor, READY_EVENT, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "pubsub subscriber ready");
            } finally {
                pubMonitor.close();
                subMonitor.close();
            }
            settleAfterReady();

            Thread recvThread = new Thread(() -> {
                long lastRecvNs = System.nanoTime();
                long flushNs = Duration.ofMillis(Math.max(1, config.recvTimeoutMs())).toNanos();
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(sub), PollEventFlag.POLLIN)) {
                    while (true) {
                        int timeoutMs = idleDrain.get() ? Math.max(1, config.recvTimeoutMs()) : -1;
                        int rc = pollSet.poll(timeoutMs);
                        if (rc > 0 && pollSet.isReady(0, PollEventFlag.POLLIN)) {
                            while (true) {
                                try (TopicMessage received = sub.subscribe(RecvFlags.DONT_WAIT)) {
                                    if (received == null) {
                                        break;
                                    }
                                    PerfUtil.Header header = PerfUtil.decodeHeader(
                                        received.firstPart(), config.size());
                                    if (header == null) {
                                        continue;
                                    }
                                    if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                        idleDrain.set(true);
                                        lastRecvNs = System.nanoTime();
                                        continue;
                                    }
                                    if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                        metrics.recordNanos(header.latencyNanos());
                                        lastRecvNs = System.nanoTime();
                                    }
                                }
                            }
                            continue;
                        }
                        if (idleDrain.get() && System.nanoTime() - lastRecvNs >= flushNs) {
                            return;
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                }
            }, "single-pubsub-recv");
            recvThread.start();

            metrics.startActiveWindow();
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    pub.publish(TOPIC, active);
                }
            }
            int cooldownBursts = Math.max(3, 3);
            for (int i = 0; i < cooldownBursts; i++) {
                try (Message cooldown = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                    pub.publish(TOPIC, cooldown);
                }
            }
            PerfUtil.join(recvThread, "pubsub receiver",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            if (failure.get() != null) {
                throw new IllegalStateException("pubsub receiver failed", failure.get());
            }
            pubCtx.shutdown();
            if (!sharedContext) {
                subCtx.shutdown();
            }
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                subCtx.close();
            }
            pubCtx.close();
        }
    }

    private static void settleAfterReady() {
        int settleMs = PerfUtil.intEnv("PERF_SINGLE_PUBSUB_READY_SETTLE_MS", 1000);
        if (settleMs <= 0) {
            return;
        }
        try {
            Thread.sleep(settleMs);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("pubsub settle interrupted", ex);
        }
    }
}
