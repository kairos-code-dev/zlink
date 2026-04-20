/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

final class PerfPubSub {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final String TOPIC = "perf.topic";

    private PerfPubSub() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pubsub");
        AtomicBoolean senderDone = new AtomicBoolean(false);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
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
            PerfUtil.waitForMonitorEvent(pubMonitor, READY_EVENTS, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "pubsub publisher ready");
            PerfUtil.waitForMonitorEvent(subMonitor, READY_EVENTS, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "pubsub subscriber ready");
            } finally {
                pubMonitor.close();
                subMonitor.close();
            }
            try {
                Thread.sleep(1_000L);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("pubsub settle interrupted", ex);
            }

            Thread recvThread = new Thread(() -> {
                long lastRecvNs = System.nanoTime();
                long flushNs = Duration.ofMillis(Math.max(1, config.recvTimeoutMs())).toNanos();
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(sub), PollEventType.POLLIN.getValue())) {
                    while (true) {
                        int timeoutMs = senderDone.get()
                            ? Math.max(1, config.recvTimeoutMs())
                            : -1;
                        int rc = pollSet.poll(timeoutMs < 0 ? 50 : timeoutMs);
                        if (rc > 0 && pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                            while (true) {
                                try {
                                    try (var received = sub.subscribe(RecvFlags.DONT_WAIT)) {
                                        PerfUtil.Header header = PerfUtil.decodeHeader(
                                            received.firstPart(), config.size());
                                        if (header == null) {
                                            continue;
                                        }
                                        if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                            metrics.recordNanos(header.latencyNanos());
                                        }
                                        lastRecvNs = System.nanoTime();
                                    }
                                } catch (dev.kairoscode.zlink.ZlinkException ex) {
                                    if (ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4) {
                                        break;
                                    }
                                    throw ex;
                                }
                            }
                            continue;
                        }
                        if (senderDone.get() && System.nanoTime() - lastRecvNs >= flushNs) {
                            return;
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                }
            }, "single-pubsub-recv");
            recvThread.start();

            long warmupEnd = System.nanoTime() + config.warmupSeconds() * 1_000_000_000L;
            while (System.nanoTime() < warmupEnd) {
                try (Message warmup = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                    SingleSendLoops.runWithRetry(() -> pub.publish(TOPIC, List.of(warmup)));
                }
            }
            metrics.startActiveWindow();
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    SingleSendLoops.runWithRetry(() -> pub.publish(TOPIC, List.of(m)));
                }
            }
            senderDone.set(true);
            PerfUtil.join(recvThread, "pubsub receiver",
                Duration.ofSeconds(config.warmupSeconds() + config.durationSeconds() + 10L));
            if (failure.get() != null) {
                throw new IllegalStateException("pubsub receiver failed", failure.get());
            }
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                subCtx.close();
            }
            pubCtx.close();
        }
    }
}
