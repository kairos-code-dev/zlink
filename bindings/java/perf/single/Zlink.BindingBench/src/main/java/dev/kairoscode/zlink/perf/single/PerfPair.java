/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.Optional;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

final class PerfPair {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final int ERRNO_ETIMEDOUT = 110;
    private static final int ERRNO_ETIMEDOUT_WIN = 10060;

    private PerfPair() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pair");
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

            try (Message primer = PerfUtil.payload(config.size(),
                     (byte) PerfUtil.PHASE_WARMUP, System.nanoTime())) {
                sender.send(List.of(primer));
            }
            long selfCheckDeadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
            boolean sawPrimer = false;
            while (System.nanoTime() < selfCheckDeadline) {
                Optional<dev.kairoscode.zlink.Received> maybe = PerfUtil.tryRecv(receiver);
                if (maybe.isEmpty()) {
                    Thread.onSpinWait();
                    continue;
                }
                try (var received = maybe.orElseThrow()) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(
                        received.firstPart(), config.size());
                    if (header != null && header.phase() == PerfUtil.PHASE_WARMUP) {
                        sawPrimer = true;
                        break;
                    }
                }
            }
            if (!sawPrimer) {
                throw new IllegalStateException("pair self-check timed out");
            }

            Thread traffic = new Thread(() -> {
                try {
                    metrics.startActiveWindow();
                    long activeEnd = System.nanoTime()
                        + config.durationSeconds() * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        try {
                            sender.send(List.of(PerfUtil.payload(config.size(),
                                (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())),
                                SendFlags.DONT_WAIT);
                        } catch (SubmitException ex) {
                            if (!isTransient(ex)) {
                                throw ex;
                            }
                            Thread.onSpinWait();
                        }
                    }
                    while (true) {
                        try {
                            sender.send(List.of(PerfUtil.payload(config.size(),
                                (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())),
                                SendFlags.DONT_WAIT);
                            return;
                        } catch (SubmitException ex) {
                            if (!isTransient(ex)) {
                                throw ex;
                            }
                            Thread.onSpinWait();
                        }
                    }
                } catch (Throwable ex) {
                    failure.compareAndSet(null, ex);
                }
            }, "single-pair-sender");
            traffic.start();
            long deadline = System.nanoTime()
                + Duration.ofSeconds(config.durationSeconds() + 10L).toNanos();
            boolean sawCooldown = false;
            dev.kairoscode.zlink.Received cooldown = null;
            while (System.nanoTime() < deadline) {
                Optional<dev.kairoscode.zlink.Received> maybe = PerfUtil.tryRecv(receiver);
                if (maybe.isPresent()) {
                    var received = maybe.orElseThrow();
                    PerfUtil.Header header = PerfUtil.decodeHeader(
                        received.firstPart(), config.size());
                    if (header == null) {
                        received.close();
                        continue;
                    }
                    if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                        try {
                            metrics.recordNanos(header.latencyNanos());
                        } finally {
                            received.close();
                        }
                        continue;
                    }
                    if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                        cooldown = received;
                        sawCooldown = true;
                        break;
                    }
                    received.close();
                    continue;
                }
                if (failure.get() != null && !traffic.isAlive()) {
                    throw new IllegalStateException("pair sender failed", failure.get());
                }
                Thread.onSpinWait();
            }
            PerfUtil.join(traffic, "pair sender", Duration.ofSeconds(10));
            if (cooldown != null) {
                cooldown.close();
            }
            if (failure.get() != null) {
                throw new IllegalStateException("pair sender failed", failure.get());
            }
            if (!sawCooldown) {
                throw new IllegalStateException("pair receiver timed out");
            }
            return metrics.finishSingle(config);
        }
    }

    private static boolean isTransient(SubmitException ex) {
        return ex.getResult() == SubmitResult.BACKPRESSURED
            || ex.getInternalErrno() == ERRNO_ETIMEDOUT
            || ex.getInternalErrno() == ERRNO_ETIMEDOUT_WIN;
    }
}
