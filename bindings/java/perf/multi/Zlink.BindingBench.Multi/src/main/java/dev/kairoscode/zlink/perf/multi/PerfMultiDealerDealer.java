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
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerDealer {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiDealerDealer() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             DealerSocket server = new DealerSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.CONNECTION_READY);
             PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                 List.of(server), PollEventType.POLLOUT.getValue())) {
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "dealer/dealer server");
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "dealer/dealer server ready");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    sendWhenWritable(server, pollSet, active, activeEnd);
                }
            }
            long cooldownDeadline = System.nanoTime() + Duration.ofSeconds(5).toNanos();
            for (int i = 0; i < Math.max(3, config.clients() * 3); i++) {
                try (Message cooldown = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                    sendWhenWritable(server, pollSet, cooldown, cooldownDeadline);
                }
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        PerfMultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
            try (Context ctx = PerfUtil.newContext(config);
                 DealerSocket client = new DealerSocket(ctx);
                 var monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY);
                 PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                     List.of(client), PollEventType.POLLIN.getValue())) {
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()),
                    "dealer/dealer client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    PerfControl.emitClientReady(config.size());
                    PerfControl.awaitStart(config.size(), "dealer/dealer client");
                    metrics.startActiveWindow();
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/dealer start", Duration.ofSeconds(10));
                long finishDeadline = System.nanoTime()
                    + Duration.ofSeconds(config.durationSeconds() + 20L).toNanos();
                while (System.nanoTime() < finishDeadline) {
                    long remainingNs = Math.max(1L, finishDeadline - System.nanoTime());
                    pollSet.setEvents(0, PollEventType.POLLIN.getValue());
                    if (pollSet.poll(Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                        Duration.ofNanos(remainingNs).toMillis()))) <= 0
                        || !pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                        continue;
                    }
                    while (true) {
                        dev.kairoscode.zlink.Received received = PerfUtil.recvNoWait(client);
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
                                return;
                            }
                            if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                                metrics.recordNanos(header.latencyNanos());
                            }
                        }
                    }
                }
                throw new IllegalStateException("dealer/dealer client cooldown timed out");
            }
        }, "multi-dd-client-" + index), config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void sendWhenWritable(DealerSocket socket, PerfSocketPollSet pollSet,
                                         Message payload, long deadlineNs) {
        while (true) {
            try {
                if (socket.send(payload, SendFlags.DONT_WAIT)) {
                    return;
                }
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
                (int) Math.min(Integer.MAX_VALUE, Duration.ofNanos(remainingNs).toMillis())));
        }
    }

    private static boolean isTransient(ZlinkException ex) {
        if (ex instanceof SubmitException submit) {
            return submit.getResult() == SubmitResult.BACKPRESSURED;
        }
        return ex.getInternalErrno() == 11 || ex.getInternalErrno() == 4;
    }
}
