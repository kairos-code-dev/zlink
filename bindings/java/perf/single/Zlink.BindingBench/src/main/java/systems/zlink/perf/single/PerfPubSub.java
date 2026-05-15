/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.PollEventFlag;
import systems.zlink.PubSocket;
import systems.zlink.RecvFlags;
import systems.zlink.SendFlags;
import systems.zlink.SocketType;
import systems.zlink.SubSocket;
import systems.zlink.TopicMessage;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

final class PerfPubSub {
    private static final MonitorEventType READY_EVENT = MonitorEventType.CONNECTION_READY;
    private static final String TOPIC = "perf.topic";

    private PerfPubSub() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pubsub");
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
            PerfUtil.applyAutoHwmMsgUnit(pub, config.size());
            PerfUtil.applyAutoHwmMsgUnit(sub, config.size());
            PerfUtil.recalculateAutoHwm(pubCtx);
            if (!sharedContext) {
                PerfUtil.recalculateAutoHwm(subCtx);
            }
            PerfUtil.printSingleMonitorAutoHwm(config, pubMonitor, "publisher",
                SocketType.PUB);
            PerfUtil.printSingleMonitorAutoHwm(config, subMonitor, "subscriber",
                SocketType.SUB);
            } finally {
                pubMonitor.close();
                subMonitor.close();
            }
            settleAfterReady();

            // PERF_SINGLE_TEST_POLICY § 1.4: receiver waits with -1 and exits
            // on wire-level stop token published on TOPIC.
            long activeEnd = System.nanoTime()
                + config.durationSeconds() * 1_000_000_000L;
            Thread recvThread = new Thread(() -> {
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(sub), PollEventFlag.POLLIN)) {
                    while (true) {
                        pollSet.poll(-1);
                        if (!pollSet.isReady(0, PollEventFlag.POLLIN)) {
                            continue;
                        }
                        boolean stop = false;
                        while (true) {
                            try (TopicMessage received = new TopicMessage()) {
                                if (!sub.subscribe(received, RecvFlags.DONT_WAIT)) {
                                    break;
                                }
                                if (PerfStopToken.isStopTokenMessage(received.firstPart())) {
                                    stop = true;
                                    break;
                                }
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header == null) {
                                    continue;
                                }
                                if (header.phase() == PerfUtil.PHASE_ACTIVE
                                    && System.nanoTime() < activeEnd) {
                                    metrics.recordNanos(header.latencyNanos());
                                }
                            }
                        }
                        if (stop) {
                            return;
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                }
            }, "single-pubsub-recv");
            recvThread.start();
            try (Message active = PerfUtil.payloadTemplate(config.size())) {
                try (PerfSocketPollSet writable = PerfSocketPollSet.fromSockets(
                    List.of(pub), PollEventFlag.POLLOUT)) {
                    while (System.nanoTime() < activeEnd) {
                        PerfUtil.resetAndWritePayload(active, config.size(),
                            (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                        if (!publishWhenWritable(pub, writable, active, activeEnd)) {
                            break;
                        }
                    }
                }
            }
            // PERF_SINGLE_TEST_POLICY § 1.4: signal phase end with stop token
            // published on the same topic so the subscriber's filter delivers it.
            try (Message stop = PerfStopToken.newMessage()) {
                long stopDeadline = System.nanoTime() + 2_000_000_000L;
                try (PerfSocketPollSet writable = PerfSocketPollSet.fromSockets(
                    List.of(pub), PollEventFlag.POLLOUT)) {
                    publishWhenWritable(pub, writable, stop, stopDeadline);
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

    private static boolean publishWhenWritable(PubSocket pub,
                                               PerfSocketPollSet writable,
                                               Message message,
                                               long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            if (pub.publish(TOPIC)
                    .message(message)
                    .flags(SendFlags.DONT_WAIT)
                    .submit()) {
                return true;
            }
            long remainingNs = deadlineNs - System.nanoTime();
            if (remainingNs <= 0) {
                return false;
            }
            int waitMs = (int) Math.max(1L,
                Math.min(Integer.MAX_VALUE, remainingNs / 1_000_000L));
            writable.poll(waitMs);
        }
        return false;
    }
}
