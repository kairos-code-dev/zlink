/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class PerfPair {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfPair() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pair");
        CountDownLatch finished = new CountDownLatch(1);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        try (Context ctx = PerfUtil.newContext(config);
             PairSocket receiver = new PairSocket(ctx);
             PairSocket sender = new PairSocket(ctx);
             var receiverMonitor = receiver.monitorOpen(MonitorEventType.CONNECTION_READY);
             var senderMonitor = sender.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            PerfUtil.applyMonitorOptions(receiverMonitor, config);
            PerfUtil.applyMonitorOptions(senderMonitor, config);
            PerfUtil.applySocketOptions(receiver, config);
            PerfUtil.applySocketOptions(sender, config);
            PerfUtil.configureServerTls(receiver, config.transport());
            PerfUtil.configureClientTls(sender, config.transport());
            receiver.bind(endpoint);
            sender.connect(endpoint);
            PerfUtil.waitForMonitorEvent(senderMonitor, READY_EVENTS, 1,
                readyTimeout, "pair sender ready");
            PerfUtil.waitForMonitorEvent(receiverMonitor, READY_EVENTS, 1,
                readyTimeout, "pair receiver ready");

            Thread receiverThread = new Thread(() -> {
                try (SocketPollSet pollSet = SocketPollSet.fromSockets(
                    List.of(receiver), PollEventType.POLLIN.getValue())) {
                    while (finished.getCount() > 0L) {
                        pollSet.poll(-1);
                        while (true) {
                            Optional<dev.kairoscode.zlink.Received> maybe = receiver.tryRecv();
                            if (maybe.isEmpty()) {
                                break;
                            }
                            try (var received = maybe.orElseThrow()) {
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header == null) {
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
            }, "single-pair-receiver");
            receiverThread.start();

            Thread traffic = SingleSendLoops.oneWaySend(
                () -> sender.send(List.of(PerfUtil.payload(config.size(),
                    (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime()))),
                () -> sender.send(List.of(PerfUtil.payload(config.size(),
                    (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime()))),
                config.durationSeconds(),
                metrics::startActiveWindow,
                failure,
                finished);
            traffic.start();
            PerfUtil.await(finished, "pair receiver",
                Duration.ofSeconds(config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "pair sender", Duration.ofSeconds(10));
            PerfUtil.join(receiverThread, "pair receiver thread", Duration.ofSeconds(10));
            if (failure.get() != null) {
                throw new IllegalStateException("pair receiver failed", failure.get());
            }
            return metrics.finishSingle(config);
        }
    }
}
