/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfPubSub {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfPubSub() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pubsub");
        String topic = "perf.topic";
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch ready = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        boolean sharedContext = "inproc".equals(config.transport());
        Context pubCtx = PerfUtil.newContext(config);
        Context subCtx = sharedContext ? pubCtx : PerfUtil.newContext(config);
        try (PubSocket pub = new PubSocket(pubCtx);
             SubSocket sub = new SubSocket(subCtx);
             var pubMonitor = pub.monitorOpen(MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            PerfUtil.applyMonitorOptions(pubMonitor, config);
            PerfUtil.applyMonitorOptions(subMonitor, config);
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.applySocketOptions(sub, config);
            pub.options().noDrop(true);
            PerfUtil.configureServerTls(pub, config.transport());
            PerfUtil.configureClientTls(sub, config.transport());
            pub.bind(endpoint);
            sub.setSubscription(topic);
            sub.connect(endpoint);
            PerfUtil.waitForMonitorEvent(pubMonitor, READY_EVENTS, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "pubsub publisher ready");
            PerfUtil.waitForMonitorEvent(subMonitor, READY_EVENTS, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "pubsub subscriber ready");
            try {
                Thread.sleep(250L);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("pubsub settle interrupted", ex);
            }

            Thread receiverThread = new Thread(() -> {
                try (SocketPollSet pollSet = SocketPollSet.fromSockets(
                    List.of(sub), PollEventType.POLLIN.getValue())) {
                    while (finished.getCount() > 0L) {
                        pollSet.poll(-1);
                        while (true) {
                            Optional<dev.kairoscode.zlink.TopicMessage> maybe = sub.trySubscribe();
                            if (maybe.isEmpty()) {
                                break;
                            }
                            try (var received = maybe.orElseThrow()) {
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header == null) {
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_WARMUP
                                    && ready.getCount() > 0L) {
                                    ready.countDown();
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                    finished.countDown();
                                    return;
                                }
                                if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                    metrics.recordNanos(header.latencyNanos());
                                }
                            }
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                    finished.countDown();
                }
            }, "single-pubsub-receiver");
            receiverThread.start();

            long readyDeadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
            while (ready.getCount() > 0L && System.nanoTime() < readyDeadline) {
                try (Message probe = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                    pub.publish(topic, List.of(probe));
                }
                try {
                    Thread.sleep(25L);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("pubsub warmup interrupted", ex);
                }
            }
            PerfUtil.await(ready, "pubsub subscriber ready", Duration.ofSeconds(10));

            Thread traffic = new Thread(() -> {
                metrics.startActiveWindow();
                long activeEnd = System.nanoTime()
                    + config.durationSeconds() * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        pub.publish(topic, List.of(m));
                    }
                }
                for (int i = 0; i < 16; i++) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        pub.publish(topic, List.of(m));
                    }
                }
            }, "single-pubsub-sender");
            traffic.start();
            PerfUtil.await(finished, "pubsub receiver",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "pubsub sender", Duration.ofSeconds(10));
            PerfUtil.join(receiverThread, "pubsub receiver thread",
                Duration.ofSeconds(10));
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
