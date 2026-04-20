/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.perf.PerfControl;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiDealerRouter() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket server = new RouterSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "dealer/router server ready");
            int stops = 0;
            Deque<PendingReply> pendingReplies = new ArrayDeque<>();
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                List.of(server), PollEventType.POLLIN.getValue())) {
                while (stops < config.clients()) {
                    int events = pendingReplies.isEmpty()
                        ? PollEventType.POLLIN.getValue()
                        : PollEventType.POLLIN.getValue() | PollEventType.POLLOUT.getValue();
                    pollSet.setEvents(0, events);
                    pollSet.poll(-1);
                    if (pollSet.isReady(0, PollEventType.POLLOUT.getValue())) {
                        flushPending(server, pendingReplies);
                    }
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
                            Message reply = received.firstPart().move();
                            PendingReply pending = new PendingReply(
                                received.routingIdOrThrow(), reply);
                            if (!pendingReplies.isEmpty()) {
                                pendingReplies.addLast(pending);
                                continue;
                            }
                            if (!server.trySend(pending.routingId(), pending.payload())) {
                                pendingReplies.addLast(pending);
                                continue;
                            }
                            pending.close();
                        }
                    }
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
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
                 Message request = PerfUtil.payloadTemplate(config.size());
                 Message stop = PerfUtil.payloadTemplate(config.size())) {
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()),
                    "dealer/router client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startActiveWindow();
                    PerfControl.emitClientReady(config.size());
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/router start", Duration.ofSeconds(10));
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(client), PollEventType.POLLIN.getValue())) {
                    long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        PerfUtil.writePayload(request, config.size(),
                            (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime());
                        sendUntilSent(client, pollSet, request);
                        if (!awaitReadable(pollSet, activeEnd)) {
                            break;
                        }
                        while (true) {
                            dev.kairoscode.zlink.Received received =
                                PerfUtil.recvNoWait(client);
                            if (received == null) {
                                break;
                            }
                            try (received) {
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header != null && header.phase() == PerfUtil.PHASE_ACTIVE) {
                                    metrics.recordNanos(header.latencyNanos() / 2L);
                                }
                            }
                        }
                    }
                    PerfUtil.writePayload(stop, config.size(),
                        (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime());
                    sendUntilSent(client, pollSet, stop);
                }
            }
        }, "multi-dr-client-" + index), config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void sendUntilSent(DealerSocket client, PerfSocketPollSet pollSet,
                                      Message part) {
        while (true) {
            if (client.trySend(part)) {
                return;
            }
            pollSet.setEvents(0, PollEventType.POLLOUT.getValue());
            pollSet.poll(-1);
        }
    }

    private static boolean awaitReadable(PerfSocketPollSet pollSet, long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            try {
                pollSet.setEvents(0, PollEventType.POLLIN.getValue());
                if (pollSet.poll(5) > 0) {
                    return true;
                }
            } catch (dev.kairoscode.zlink.ZlinkException ex) {
                if (ex.getInternalErrno() != 11 && ex.getInternalErrno() != 4) {
                    throw ex;
                }
            }
        }
        return false;
    }

    private static void flushPending(RouterSocket server,
                                     Deque<PendingReply> pendingReplies) {
        while (!pendingReplies.isEmpty()) {
            PendingReply pending = pendingReplies.peekFirst();
            if (pending == null) {
                return;
            }
            if (!server.trySend(pending.routingId(), pending.payload())) {
                return;
            }
            pendingReplies.removeFirst();
            pending.close();
        }
    }

    private record PendingReply(RoutingId routingId, Message payload) {
        private void close() {
            payload.close();
        }
    }
}
