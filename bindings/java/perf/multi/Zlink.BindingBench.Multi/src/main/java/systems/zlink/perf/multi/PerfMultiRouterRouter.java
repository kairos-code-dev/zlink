/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.PollEventFlag;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfStopToken;
import systems.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Deque;
import java.util.List;

final class PerfMultiRouterRouter {
    private static final MonitorEventType READY_EVENT =
        MonitorEventType.CONNECTION_READY;
    private static final RoutingId SERVER_ID = RoutingId.fromBytes(
        "PERF_SERVER".getBytes(StandardCharsets.UTF_8));

    private PerfMultiRouterRouter() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket server = new RouterSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            server.setRoutingId(SERVER_ID);
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENT, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "router/router server ready");
            PerfUtil.applyAutoHwmMsgUnit(server, config.size());
            PerfUtil.recalculateAutoHwm(ctx);
            int stops = 0;
            Deque<PendingReply> pendingReplies = new ArrayDeque<>();
            // Long-lived caller-provided Received reused across every recv on
            // the server hot path. The binding refills its internal state in
            // place via adoptFrom, avoiding the per-recv Received allocation
            // that the legacy `recv() -> Received` path forced. Matches the
            // C++/.NET canonical caller-provided storage pattern.
            systems.zlink.Received receivedBuffer = new systems.zlink.Received();
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                List.of(server), PollEventFlag.POLLIN)) {
                // PERF_MULTI_TEST_POLICY § 1.2 echo server: DONT_WAIT + per-socket
                // pending deque, drain POLLIN until EAGAIN, drain POLLOUT to flush
                // pending. § 1.3.1: signal-driven wait (-1).
                while (stops < config.clients()) {
                    if (pendingReplies.isEmpty()) {
                        pollSet.setEvents(0, PollEventFlag.POLLIN);
                    } else {
                        pollSet.setEvents(0,
                            PollEventFlag.POLLIN, PollEventFlag.POLLOUT);
                    }
                    pollSet.poll(-1);
                    boolean writable =
                        pollSet.isReady(0, PollEventFlag.POLLOUT);
                    boolean readable =
                        pollSet.isReady(0, PollEventFlag.POLLIN);
                    if (writable) {
                        flushPending(server, pendingReplies);
                    }
                    if (!readable) {
                        continue;
                    }
                    while (true) {
                        boolean ok;
                        try {
                            ok = server.recv(receivedBuffer, RecvFlags.DONT_WAIT);
                        } catch (RecvException ex) {
                            if (ex.getResult() == RecvResult.NO_DATA
                                || ex.getResult() == RecvResult.BUSY) {
                                break;
                            }
                            throw ex;
                        }
                        if (!ok) break;

                        if (PerfStopToken.isStopTokenMessage(
                                receivedBuffer.firstPart())) {
                            stops++;
                            receivedBuffer.close();
                            continue;
                        }
                        PerfUtil.Header header = PerfUtil.decodeHeader(
                            receivedBuffer.firstPart(), config.size());
                        if (header == null) {
                            receivedBuffer.close();
                            continue;
                        }
                        RoutingId rid = receivedBuffer.routingId().orElseThrow();
                        // Fast path: send directly when no pending backlog.
                        if (pendingReplies.isEmpty()
                            && server.send(rid, receivedBuffer.firstPart(),
                                SendFlags.DONT_WAIT)) {
                            receivedBuffer.close();
                            continue;
                        }
                        // Slow path: take ownership of the part to outlive
                        // the Received scope and enqueue / send.
                        Message ownedReply = receivedBuffer.firstPart().move();
                        receivedBuffer.close();
                        if (pendingReplies.isEmpty()
                            && server.send(rid, ownedReply,
                                SendFlags.DONT_WAIT)) {
                            ownedReply.close();
                        } else {
                            pendingReplies.addLast(
                                new PendingReply(rid, ownedReply));
                        }
                    }
                }
            }
            return PerfUtil.Result.silent(config);
        }
    }

    // PERF_MULTI_TEST_POLICY §1.3 mandates a single app thread driving the
    // poller event loop for all N client sockets, mirroring the C reference
    // perf_multi_client_helpers.hpp::run_echo_window_round_robin. This
    // single-thread + single-context + N-sockets model is the canonical
    // multi-client measurement structure; the previous per-thread + per-
    // context fan-out diverged from policy and inflated measurement noise
    // (per-context I/O dispatcher overhead, synchronized metric collection).
    static PerfUtil.Result runClient(PerfUtil.Config config) {
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        int clientCount = Math.max(1, config.clients());
        int durationSeconds = Math.max(1, config.durationSeconds());

        try (Context ctx = PerfUtil.newContext(config)) {
            List<RouterSocket> clients = new ArrayList<>(clientCount);
            List<systems.zlink.MonitorSocket> monitors =
                new ArrayList<>(clientCount);
            try {
                for (int i = 0; i < clientCount; i++) {
                    RouterSocket client = new RouterSocket(ctx);
                    client.setRoutingId(RoutingId.fromBytes(
                        ("PERF_CLIENT_" + i).getBytes(StandardCharsets.UTF_8)));
                    client.options().connectRoutingId(SERVER_ID);
                    var monitor = client.monitorOpen(
                        MonitorEventType.CONNECTION_READY);
                    PerfUtil.applyMonitorOptions(monitor, config);
                    PerfUtil.applySocketOptions(client, config);
                    PerfUtil.configureClientTls(client, config.transport());
                    client.connect(config.endpoint());
                    clients.add(client);
                    monitors.add(monitor);
                }
                for (int i = 0; i < clientCount; i++) {
                    PerfUtil.waitForMonitorEvent(monitors.get(i), READY_EVENT, 1,
                        Duration.ofMillis(config.connectReadyTimeoutMs()),
                        "router/router client ready[" + i + "]");
                }
                for (int i = 0; i < clientCount; i++) {
                    PerfUtil.applyAutoHwmMsgUnit(clients.get(i), config.size());
                }
                PerfUtil.recalculateAutoHwm(ctx);
                for (int i = 0; i < clientCount; i++) {
                    monitors.get(i).close();
                }
                monitors.clear();

                runRouterRouterClientLoop(ctx, clients, config, durationSeconds,
                    metrics);
            } finally {
                for (var monitor : monitors) {
                    try { monitor.close(); } catch (RuntimeException ignored) {}
                }
                for (RouterSocket client : clients) {
                    try { client.close(); } catch (RuntimeException ignored) {}
                }
            }
        }
        return metrics.finishMulti(config);
    }

    // Single-thread round-robin: one PollSet over all client sockets, mirror
    // of C perf_multi_client_helpers.hpp::run_echo_window_round_robin and
    // .NET PerfMultiRouterRouterClient.RunMultiRouterRouterClientLoop.
    private static void runRouterRouterClientLoop(Context ctx,
                                                  List<RouterSocket> clients,
                                                  PerfUtil.Config config,
                                                  int durationSeconds,
                                                  PerfUtil.Metrics metrics) {
        int n = clients.size();
        int msgSize = config.size();
        int runId = PerfUtil.runId();
        // Reusable per-slot send payload buffer. C reference reuses one
        // payload buffer across the entire active phase (or per-socket when
        // borrow_payload_per_socket=true). We use per-socket buffers so that
        // an inflight=1 send isn't disturbed by the next slot's stamp.
        byte[][] payloads = new byte[n][];
        boolean[] waitingReply = new boolean[n];
        boolean[] waitingWritable = new boolean[n];
        for (int i = 0; i < n; i++) {
            payloads[i] = new byte[Math.max(msgSize, PerfUtil.HEADER_SIZE)];
            Arrays.fill(payloads[i], (byte) 'a');
        }
        List<systems.zlink.Socket> socketsAsBase = new ArrayList<>(n);
        for (RouterSocket c : clients) {
            socketsAsBase.add(c);
        }
        long seq = 1L;
        int rrIndex = 0;
        // Long-lived Received reused across recv on every client socket. The
        // canonical ref-out recv refills it in place via populateRoutedSinglePart,
        // avoiding the per-recv Received + ArrayList allocation.
        systems.zlink.Received replyBuffer = new systems.zlink.Received();
        try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                socketsAsBase, PollEventFlag.POLLIN)) {
            metrics.startActiveWindow();
            long activeEnd = System.nanoTime()
                + (long) durationSeconds * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                int startIndex = rrIndex;
                for (int i = 0; i < n; i++) {
                    int idx = (startIndex + i) % n;
                    if (waitingReply[idx] || waitingWritable[idx]) continue;
                    stampMetricHeader(payloads[idx], runId,
                        (byte) PerfUtil.PHASE_ACTIVE, msgSize, seq++,
                        System.nanoTime());
                    if (trySendPayload(clients.get(idx), payloads[idx])) {
                        waitingReply[idx] = true;
                    } else {
                        waitingWritable[idx] = true;
                    }
                    updatePollMask(pollSet, idx, waitingReply[idx],
                        waitingWritable[idx]);
                }
                rrIndex = (startIndex + 1) % n;
                pollSet.poll(-1);
                for (int idx = 0; idx < n; idx++) {
                    boolean writable =
                        pollSet.isReady(idx, PollEventFlag.POLLOUT);
                    if (writable && waitingWritable[idx] && !waitingReply[idx]) {
                        if (trySendPayload(clients.get(idx), payloads[idx])) {
                            waitingWritable[idx] = false;
                            waitingReply[idx] = true;
                            updatePollMask(pollSet, idx, true, false);
                        }
                    }
                    boolean readable =
                        pollSet.isReady(idx, PollEventFlag.POLLIN);
                    if (!readable && !waitingReply[idx]) continue;
                    drainReplies(clients.get(idx), idx, waitingReply,
                        waitingWritable, msgSize, runId, metrics, pollSet,
                        replyBuffer);
                }
            }
            // PERF_MULTI_TEST_POLICY §1.3.1 wire-level stop token: one per
            // client. Best-effort send, do not block the runner past the
            // result-line timeout.
            for (int i = 0; i < n; i++) {
                try (Message stop = PerfStopToken.newMessage()) {
                    clients.get(i).send(SERVER_ID, stop, SendFlags.DONT_WAIT);
                }
            }
        }
        // ctx auto-HWM was already applied at setup; reference kept for the
        // compiler so the parameter isn't flagged unused.
        if (ctx == null) {
            throw new IllegalStateException("ctx must not be null");
        }
    }

    private static void drainReplies(RouterSocket client, int idx,
                                     boolean[] waitingReply,
                                     boolean[] waitingWritable,
                                     int msgSize, int runId,
                                     PerfUtil.Metrics metrics,
                                     PerfSocketPollSet pollSet,
                                     systems.zlink.Received replyBuffer) {
        while (true) {
            boolean ok;
            try {
                ok = client.recv(replyBuffer, RecvFlags.DONT_WAIT);
            } catch (RecvException ex) {
                if (ex.getResult() == RecvResult.NO_DATA
                    || ex.getResult() == RecvResult.BUSY) {
                    break;
                }
                throw ex;
            }
            if (!ok) break;
            if (!waitingReply[idx]) {
                replyBuffer.close();
                continue;
            }
            waitingReply[idx] = false;
            PerfUtil.Header header = PerfUtil.decodeHeader(
                replyBuffer.firstPart(), msgSize);
            if (header != null
                && header.phase() == PerfUtil.PHASE_ACTIVE) {
                metrics.recordNanos(header.latencyNanos() / 2L);
            }
            waitingWritable[idx] = false;
            updatePollMask(pollSet, idx, false, false);
            replyBuffer.close();
        }
    }

    private static boolean trySendPayload(RouterSocket client, byte[] payload) {
        try (Message message = Message.copyOf(payload)) {
            return client.send(SERVER_ID, message, SendFlags.DONT_WAIT);
        }
    }

    // Stamps the canonical 29-byte metric header (PERF_POLICY §1.1.1) at the
    // start of an existing send buffer. Mirrors C reference
    // stamp_metric_payload in perf_multi_metric_header.hpp.
    private static final int METRIC_MAGIC = 0x5A4C4E4B; // "ZLNK"

    private static void stampMetricHeader(byte[] buf, int runId, byte phase,
                                          int msgSize, long seq, long sentTsNs) {
        writeIntLe(buf, 0, METRIC_MAGIC);
        writeIntLe(buf, 4, runId);
        buf[8] = phase;
        writeIntLe(buf, 9, msgSize);
        writeLongLe(buf, 13, seq);
        writeLongLe(buf, 21, sentTsNs);
    }

    private static void writeIntLe(byte[] buf, int offset, int value) {
        buf[offset] = (byte) value;
        buf[offset + 1] = (byte) (value >>> 8);
        buf[offset + 2] = (byte) (value >>> 16);
        buf[offset + 3] = (byte) (value >>> 24);
    }

    private static void writeLongLe(byte[] buf, int offset, long value) {
        buf[offset] = (byte) value;
        buf[offset + 1] = (byte) (value >>> 8);
        buf[offset + 2] = (byte) (value >>> 16);
        buf[offset + 3] = (byte) (value >>> 24);
        buf[offset + 4] = (byte) (value >>> 32);
        buf[offset + 5] = (byte) (value >>> 40);
        buf[offset + 6] = (byte) (value >>> 48);
        buf[offset + 7] = (byte) (value >>> 56);
    }

    private static void updatePollMask(PerfSocketPollSet pollSet, int idx,
                                       boolean waitingReply,
                                       boolean waitingWritable) {
        if (waitingWritable && !waitingReply) {
            pollSet.setEvents(idx, PollEventFlag.POLLIN,
                PollEventFlag.POLLOUT);
        } else {
            pollSet.setEvents(idx, PollEventFlag.POLLIN);
        }
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
