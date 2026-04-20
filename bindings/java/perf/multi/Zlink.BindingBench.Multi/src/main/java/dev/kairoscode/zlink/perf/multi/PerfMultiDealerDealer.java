/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.perf.PerfControl;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerDealer {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_ETIMEDOUT = 110;
    private static final int ERRNO_ETIMEDOUT_WIN = 10060;

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
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "dealer/dealer server");
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "dealer/dealer server ready");
            PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
            metrics.startActiveWindow();
            int stops = 0;
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                java.util.List.of(server), PollEventType.POLLIN.getValue())) {
                while (stops < config.clients()) {
                    pollSet.poll(-1);
                    while (true) {
                        dev.kairoscode.zlink.Received received =
                            PerfUtil.recvNoWait(server);
                        if (received == null) {
                            break;
                        }
                        try (received) {
                            PerfUtil.Header header = PerfUtil.decodeHeader(
                                received.firstPart(), config.size());
                            if (header == null) {
                                continue;
                            }
                            if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                stops++;
                                continue;
                            }
                            if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                metrics.recordNanos(header.latencyNanos());
                            }
                        }
                    }
                }
            }
            return metrics.finishMulti(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        Context ctx = PerfUtil.newContext(config);
        PerfMultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
                try (DealerSocket client = new DealerSocket(ctx);
                     var monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY);
                     PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                         java.util.List.of(client), PollEventType.POLLOUT.getValue());
                     Message active = PerfUtil.payloadTemplate(config.size());
                     Message cooldown = PerfUtil.payloadTemplate(config.size())) {
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
                        PerfControl.emitClientReady(config.size());
                        PerfControl.awaitStart(config.size(), "dealer/dealer client");
                        go.countDown();
                    }
                    PerfUtil.await(go, "dealer/dealer start", java.time.Duration.ofSeconds(10));
                    long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        PerfUtil.writePayload(active, config.size(),
                            (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                        sendWhenWritable(client, pollSet, active, activeEnd);
                    }
                    PerfUtil.writePayload(cooldown, config.size(),
                        (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime());
                    sendWhenWritable(client, pollSet, cooldown,
                        System.nanoTime() + Duration.ofSeconds(5).toNanos());
                }
            }, "multi-dd-client-" + index), config.durationSeconds());
        return PerfUtil.Result.silent(config);
    }

    private static void sendWhenWritable(DealerSocket client, PerfSocketPollSet pollSet,
                                         Message part, long deadlineNs) {
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
                throw new IllegalStateException("dealer/dealer send timed out");
            }
            long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
            pollSet.setEvents(0, PollEventType.POLLOUT.getValue());
            pollSet.poll(Math.max(1,
                (int) Math.min(Integer.MAX_VALUE,
                    Duration.ofNanos(remainingNs).toMillis())));
        }
    }

    private static boolean isTransient(ZlinkException ex) {
        if (ex instanceof SubmitException submit) {
            return submit.getResult() == SubmitResult.BACKPRESSURED;
        }
        return ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4;
    }
}
