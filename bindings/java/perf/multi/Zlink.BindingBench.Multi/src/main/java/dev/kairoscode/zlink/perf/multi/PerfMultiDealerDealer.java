/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerDealer {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiDealerDealer() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             DealerSocket server = new DealerSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            PerfUtil.Metrics metrics = new PerfUtil.Metrics();
            metrics.startActiveWindow();
            long stopAt = System.nanoTime()
                + Duration.ofSeconds(config.durationSeconds()).plusMillis(500L).toNanos();
            while (System.nanoTime() < stopAt) {
                try (var received = server.recv()) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(
                        received.firstPart(), config.size());
                    if (header == null) {
                        continue;
                    }
                    if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordNanos(header.latencyNanos());
                    }
                } catch (ZlinkException ex) {
                    if (ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4) {
                        continue;
                    }
                    throw ex;
                }
            }
            return metrics.finishMulti(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        Context ctx = PerfUtil.newContext(config);
        MultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
                DealerSocket client = new DealerSocket(ctx);
                var monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY);
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                client.options().linger(Duration.ZERO);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()),
                    "dealer/dealer client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/dealer start", java.time.Duration.ofSeconds(10));
                long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        sendUntilDeadline(client, m, activeEnd);
                    }
                }
                long cooldownDeadline =
                    System.nanoTime() + Duration.ofMillis(50L).toNanos();
                for (int i = 0; i < 4; i++) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        sendUntilDeadline(client, m, cooldownDeadline);
                    }
                }
                try {
                    Thread.sleep(50L);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("dealer/dealer cooldown interrupted", ex);
                }
            }, "multi-dd-client-" + index), config.durationSeconds());
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
            config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
    }

    private static void sendUntilDeadline(DealerSocket client, Message part, long deadlineNs) {
        int attempts = 0;
        while (true) {
            try {
                client.send(part, SendFlags.DONT_WAIT);
                return;
            } catch (SubmitException ex) {
                if (!isTransient(ex)) {
                    throw ex;
                }
            } catch (ZlinkException ex) {
                if (!isTransient(ex)) {
                    throw ex;
                }
            }

            if (System.nanoTime() >= deadlineNs) {
                return;
            }
            attempts++;
            if (attempts < 32) {
                Thread.onSpinWait();
                continue;
            }
            try {
                Thread.sleep(1L);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("dealer/dealer retry interrupted", ex);
            }
        }
    }

    private static boolean isTransient(ZlinkException ex) {
        if (ex instanceof SubmitException submit) {
            return submit.getResult() == SubmitResult.BACKPRESSURED
                || submit.getResult() == SubmitResult.NOT_CONNECTED;
        }
        return ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4;
    }
}
