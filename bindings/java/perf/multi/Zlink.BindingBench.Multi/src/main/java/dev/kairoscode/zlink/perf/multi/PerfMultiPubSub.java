/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RecvFlags;
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
            PerfControl.awaitStart(config.size(), "pubsub server");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    pub.publish(TOPIC, List.of(m));
                }
            }
            for (int i = 0; i < Math.max(3, config.clients() * 3); i++) {
                try (Message m = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                    pub.publish(TOPIC, List.of(m));
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        MultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
            try (Context ctx = PerfUtil.newContext(config);
                 SubSocket sub = new SubSocket(ctx)) {
                PerfUtil.applySocketOptions(sub, config);
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription(TOPIC);
                sub.connect(config.endpoint());
                connected.countDown();
                if (connected.getCount() == 0L) {
                    PerfControl.emitClientReady(config.size());
                    PerfControl.awaitStart(config.size(), "pubsub client");
                    metrics.startActiveWindow();
                    go.countDown();
                }
                PerfUtil.await(go, "pubsub start", Duration.ofSeconds(10));
                long finishDeadline = System.nanoTime()
                    + Duration.ofSeconds(config.durationSeconds() + 20L).toNanos();
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
                            TopicMessage received;
                            try {
                                received = sub.subscribe(RecvFlags.DONT_WAIT);
                            } catch (dev.kairoscode.zlink.ZlinkException ex) {
                                if (ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4) {
                                    break;
                                }
                                throw ex;
                            }
                            try (received) {
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
}
