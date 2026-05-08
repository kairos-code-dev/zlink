/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.PollEventFlag;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerRouter {
    private static final MonitorEventType READY_EVENT =
        MonitorEventType.CONNECTION_READY;

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
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENT, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "dealer/router server ready");
            int stops = 0;
            Deque<PendingReply> pendingReplies = new ArrayDeque<>();
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                List.of(server), PollEventFlag.POLLIN)) {
                while (stops < config.clients()) {
                    if (pendingReplies.isEmpty()) {
                        pollSet.setEvents(0, PollEventFlag.POLLIN);
                    } else {
                        pollSet.setEvents(0, PollEventFlag.POLLIN,
                            PollEventFlag.POLLOUT);
                    }
                    pollSet.poll(-1);
                    if (pollSet.isReady(0, PollEventFlag.POLLOUT)) {
                        flushPending(server, pendingReplies);
                    }
                    while (true) {
                        systems.zlink.Received received =
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
                                received.routingId().orElseThrow(), reply);
                            if (!pendingReplies.isEmpty()) {
                                pendingReplies.addLast(pending);
                                continue;
                            }
                            if (!server.send(pending.routingId(), pending.payload(),
                                SendFlags.DONT_WAIT)) {
                                pendingReplies.addLast(pending);
                                continue;
                            }
                            pending.close();
                        }
                    }
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
                 var monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENT, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()),
                    "dealer/router client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startActiveWindow();
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/router start", Duration.ofSeconds(10));
                try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                    List.of(client), PollEventFlag.POLLIN)) {
                    long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        try (Message request = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                            sendUntilSent(client, pollSet, request, activeEnd);
                        }
                        if (!awaitReadable(pollSet, activeEnd)) {
                            break;
                        }
                        while (true) {
                            systems.zlink.Received received =
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
                    try (Message stop = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                        sendUntilSent(client, pollSet, stop,
                            System.nanoTime() + Duration.ofSeconds(5).toNanos());
                    }
                }
            }
        }, "multi-dr-client-" + index), config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void sendUntilSent(DealerSocket client, PerfSocketPollSet pollSet,
                                      Message part,
                                      long deadlineNs) {
        while (true) {
            if (client.send(part, SendFlags.DONT_WAIT)) {
                return;
            }
            if (System.nanoTime() >= deadlineNs) {
                throw new IllegalStateException("dealer/router send timed out");
            }
            long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
            pollSet.setEvents(0, PollEventFlag.POLLOUT);
            pollSet.poll(Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                Duration.ofNanos(remainingNs).toMillis())));
        }
    }

    private static boolean awaitReadable(PerfSocketPollSet pollSet, long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            try {
                long remainingNs = Math.max(1L, deadlineNs - System.nanoTime());
                pollSet.setEvents(0, PollEventFlag.POLLIN);
                if (pollSet.poll(Math.max(1, (int) Math.min(Integer.MAX_VALUE,
                    Duration.ofNanos(remainingNs).toMillis()))) > 0) {
                    return true;
                }
            } catch (systems.zlink.ZlinkException ex) {
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
            if (!server.send(pending.routingId(), pending.payload(),
                SendFlags.DONT_WAIT)) {
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
