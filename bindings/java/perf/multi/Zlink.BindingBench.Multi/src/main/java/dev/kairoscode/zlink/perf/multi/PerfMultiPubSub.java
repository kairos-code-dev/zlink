/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.perf.PerfControl;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfMultiPubSub {
    private static final String TOPIC = "perf.topic";

    private PerfMultiPubSub() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             PubSocket pub = new PubSocket(ctx)) {
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.configureServerTls(pub, config.transport());
            pub.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            sleepMillis(500);
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    pub.publish(TOPIC, active, SendFlags.DONT_WAIT);
                }
            }
            long cooldownEnd = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (System.nanoTime() < cooldownEnd) {
                try (Message cooldown = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                    pub.publish(TOPIC, cooldown, SendFlags.DONT_WAIT);
                }
                sleepMillis(1);
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        PerfMultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
            try (Context ctx = PerfUtil.newContext(config);
                 SubSocket sub = new SubSocket(ctx)) {
                PerfUtil.applySocketOptions(sub, config);
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription(TOPIC);
                sub.connect(config.endpoint());
                connected.countDown();
                if (connected.getCount() == 0L) {
                    PerfControl.emitClientReady(config.size());
                    metrics.startActiveWindow();
                    go.countDown();
                }
                PerfUtil.await(go, "pubsub start", Duration.ofSeconds(10));
                long finishDeadline = System.nanoTime()
                    + Duration.ofSeconds(config.durationSeconds() + 3L).toNanos();
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(sub), PollEventType.POLLIN.getValue())) {
                    while (System.nanoTime() < finishDeadline) {
                        int timeoutMs = Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                            Duration.ofNanos(
                                Math.max(1L, finishDeadline - System.nanoTime())).toMillis()));
                        if (pollSet.poll(timeoutMs) <= 0
                            || !pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                            continue;
                        }
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
                                    return;
                                }
                                if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                    metrics.recordNanos(header.latencyNanos());
                                }
                            }
                        }
                    }
                }
            } catch (Throwable ex) {
                failure.compareAndSet(null, ex);
            }
        }, "multi-pubsub-client-" + index), config.durationSeconds());
        if (failure.get() != null) {
            throw new IllegalStateException("pubsub client failed", failure.get());
        }
        return metrics.finishMulti(config);
    }

    private static void sleepMillis(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("pubsub sleep interrupted", ex);
        }
    }
}
