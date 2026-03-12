/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_DEALER_ROUTER client benchmark.
 * DEALER(connect) sends+receives echo payloads in round-robin.
 */
public final class PerfMultiDealerRouterClient {
    private static final String PATTERN = "MULTI_DEALER_ROUTER";
    private static final SocketType CLIENT_SOCKET_TYPE = SocketType.DEALER;
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private static final class PendingSend {
        boolean pending;
        final ByteBuf payload;

        PendingSend(int payloadSize) {
            this.payload = Unpooled.directBuffer(payloadSize, payloadSize);
            this.payload.writeZero(payloadSize);
            reset();
        }

        void reset() {
            payload.readerIndex(0);
            payload.writerIndex(payload.capacity());
        }

        void release() {
            payload.release();
        }
    }

    private static final class ScheduleResult {
        long nextSeq;
        final int nextIndex;
        boolean progressed;

        ScheduleResult(long nextSeq, int nextIndex, boolean progressed) {
            this.nextSeq = nextSeq;
            this.nextIndex = nextIndex;
            this.progressed = progressed;
        }
    }

    private PerfMultiDealerRouterClient() {
    }

    public static int runClient(String transport, int msgSize, String endpoint) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }
        if (endpoint == null || endpoint.isBlank()) {
            return 1;
        }

        int clients = PerfMultiCommon.resolveClients(PATTERN);
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
        int pollTimeoutMs = PerfMultiCommon.resolveEffectiveClientPollTimeoutMs();
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(clients);
            List<MonitorSocket> monitors = new ArrayList<>(clients);
            List<Socket> activeSockets = new ArrayList<>(clients);
            PendingSend[] pendingSends = null;

            try {
                for (int i = 0; i < clients; i++) {
                    Socket socket = new Socket(context, CLIENT_SOCKET_TYPE);
                    applySocketOptions(socket);
                    PerfMultiTls.configureTlsClientIfNeeded(socket, transport);
                    MonitorSocket monitor = socket.monitorOpen(
                        PerfMultiCommon.CONNECT_MONITOR_EVENTS);
                    monitor.setOption(SocketOptions.RCVHWM,
                        PerfMultiCommon.resolveMonitorHwm());
                    socket.connect(endpoint);
                    sockets.add(socket);
                    monitors.add(monitor);
                }

                if (!PerfMultiCommon.waitAllConnectReady(monitors,
                        connectTimeoutMs)) {
                    return 2;
                }
                closeAll(monitors);
                monitors.clear();
                activeSockets.addAll(sockets);
                if (activeSockets.size() != clients) {
                    return 2;
                }

                pendingSends = new PendingSend[activeSockets.size()];
                for (int i = 0; i < pendingSends.length; i++) {
                    pendingSends[i] = new PendingSend(payloadSize);
                }
                ByteBuf recv = Unpooled.directBuffer(payloadSize, payloadSize);
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
                long seq = 1;
                int index = 0;
                boolean[] awaitingReply = new boolean[activeSockets.size()];

                try (SocketPollSet pollSet = SocketPollSet.fromSockets(
                    activeSockets, PollEventType.POLLIN.getValue())) {
                    long warmupDeadline = System.nanoTime()
                        + (long) Math.max(0, warmupSeconds)
                        * NANOSECONDS_PER_SECOND;
                    while (System.nanoTime() < warmupDeadline) {
                        ScheduleResult schedule = scheduleIdleSends(activeSockets,
                            pendingSends, awaitingReply, pollSet, msgSize, runId,
                            PerfMultiMetricHeader.PHASE_WARMUP, seq, index);
                        seq = schedule.nextSeq;
                        index = schedule.nextIndex;
                        int readyCount = pollSet.poll(schedule.progressed ? 0
                            : pollTimeoutMs);
                        if (readyCount > 0) {
                            processReadySockets(pollSet, pendingSends,
                                awaitingReply, recv, null, schedule, msgSize,
                                runId, PerfMultiMetricHeader.PHASE_WARMUP,
                                true);
                        }
                        seq = schedule.nextSeq;
                    }

                    runDrainPhase(activeSockets, pendingSends, awaitingReply,
                        pollSet, recv, pollTimeoutMs, settleMs);
                    if (!awaitPendingRepliesDrained(activeSockets, pendingSends,
                            awaitingReply, pollSet, recv, pollTimeoutMs,
                            connectTimeoutMs, settleMs)) {
                        return 2;
                    }

                    PerfCommon.LatencyReservoir reservoir =
                        new PerfCommon.LatencyReservoir(
                            PerfMultiCommon.resolveLatencySampleCap());
                    PerfMultiMetricHeader.Header header =
                        new PerfMultiMetricHeader.Header();

                    long count = 0;
                    long benchDeadline = System.nanoTime()
                        + (long) Math.max(1, durationSeconds)
                        * NANOSECONDS_PER_SECOND;

                    while (System.nanoTime() < benchDeadline) {
                        ScheduleResult schedule = scheduleIdleSends(activeSockets,
                            pendingSends, awaitingReply, pollSet, msgSize, runId,
                            PerfMultiMetricHeader.PHASE_ACTIVE, seq, index);
                        seq = schedule.nextSeq;
                        index = schedule.nextIndex;
                        int readyCount = pollSet.poll(schedule.progressed ? 0
                            : pollTimeoutMs);
                        if (readyCount > 0) {
                            count += processReadySockets(pollSet, pendingSends,
                                awaitingReply, recv, header, schedule, msgSize,
                                runId, PerfMultiMetricHeader.PHASE_ACTIVE, true,
                                reservoir);
                        }
                        seq = schedule.nextSeq;
                    }

                    try {
                        sendImmediate(activeSockets.get(0), STOP_TOKEN);
                    } catch (RuntimeException ignored) {
                    }

                    double configuredSeconds = Math.max(1.0, durationSeconds);
                    double throughput = count / configuredSeconds;
                    PerfCommon.Stats stats = reservoir.snapshot();
                    PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                        stats.meanUs(), stats.p95Us(), stats.p99Us());
                    recv.release();
                    return 0;
                }
            } finally {
                releasePending(pendingSends);
                closeAll(monitors);
                closeAll(sockets);
                try {
                    context.shutdown();
                } catch (RuntimeException ignored) {
                }
            }
        } catch (RuntimeException ex) {
            String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
                : ex.getMessage();
            System.err.println("multi_dealer_router_client_error:" + message);
            return 2;
        }
    }

    private static void applySocketOptions(Socket socket) {
        socket.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        socket.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        socket.setOption(SocketOptions.LINGER, 0);
        socket.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        socket.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
    }

    private static ScheduleResult scheduleIdleSends(List<Socket> activeSockets,
                                                    PendingSend[] pendingSends,
                                                    boolean[] awaitingReply,
                                                    SocketPollSet pollSet,
                                                    int msgSize,
                                                    int runId,
                                                    int phase,
                                                    long seqStart,
                                                    int indexStart) {
        long seq = seqStart;
        boolean progressed = false;
        int socketCount = activeSockets.size();
        int startIndex = indexStart;
        for (int attempt = 0; attempt < socketCount; attempt++) {
            int socketIndex = (startIndex + attempt) % socketCount;
            if (awaitingReply[socketIndex] || pendingSends[socketIndex].pending) {
                continue;
            }

            if (tryStartSend(pollSet, activeSockets.get(socketIndex),
                    socketIndex, pendingSends[socketIndex], awaitingReply,
                    msgSize, runId, phase, seq)) {
                progressed = true;
                seq++;
            }
        }
        return new ScheduleResult(seq,
            socketCount == 0 ? 0 : (startIndex + 1) % socketCount,
            progressed);
    }

    private static void runDrainPhase(List<Socket> activeSockets,
                                      PendingSend[] pendingSends,
                                      boolean[] awaitingReply,
                                      SocketPollSet pollSet,
                                      ByteBuf recv,
                                      int pollTimeoutMs,
                                      int settleMs) {
        if (settleMs <= 0) {
            return;
        }

        long deadlineNs = System.nanoTime()
            + (long) Math.max(0, settleMs) * 1_000_000L;
        while (System.nanoTime() < deadlineNs) {
            int readyCount = pollSet.poll(Math.max(1, pollTimeoutMs));
            if (readyCount <= 0) {
                continue;
            }
            processReadySockets(pollSet, pendingSends, awaitingReply, recv,
                null, null, 0, 0, 0, false);
            for (int i = 0; i < activeSockets.size(); i++) {
                if (!awaitingReply[i] && !pendingSends[i].pending) {
                    pollSet.setEvents(i, PollEventType.POLLIN.getValue());
                }
            }
        }
    }

    private static boolean tryStartSend(SocketPollSet pollSet, Socket socket,
                                        int index,
                                        PendingSend pending,
                                        boolean[] awaitingReply,
                                        int msgSize,
                                        int runId,
                                        int phase,
                                        long seq) {
        pending.reset();
        PerfMultiMetricHeader.stampPayload(pending.payload, runId, phase,
            msgSize, seq, PerfMultiMetricHeader.nowUs());
        if (trySendNonBlocking(socket, pending)) {
            awaitingReply[index] = true;
            return true;
        }
        if (!pending.pending) {
            pending.pending = true;
            pollSet.setEvents(index, PollEventType.POLLIN.getValue()
                | PollEventType.POLLOUT.getValue());
        }
        return false;
    }

    private static boolean tryFlushPending(SocketPollSet pollSet, Socket socket,
                                           int index, PendingSend pending,
                                           boolean[] awaitingReply) {
        if (!pending.pending) {
            return false;
        }
        if (!trySendNonBlocking(socket, pending)) {
            return false;
        }
        pending.pending = false;
        awaitingReply[index] = true;
        pollSet.setEvents(index, PollEventType.POLLIN.getValue());
        return true;
    }

    private static long processReadySockets(SocketPollSet pollSet,
                                            PendingSend[] pendingSends,
                                            boolean[] awaitingReply,
                                            ByteBuf recv,
                                            PerfMultiMetricHeader.Header header,
                                            ScheduleResult schedule,
                                            int msgSize,
                                            int runId,
                                            int phase,
                                            boolean allowSend) {
        return processReadySockets(pollSet, pendingSends, awaitingReply, recv,
            header, schedule, msgSize, runId, phase, allowSend, null);
    }

    private static long processReadySockets(SocketPollSet pollSet,
                                            PendingSend[] pendingSends,
                                            boolean[] awaitingReply,
                                            ByteBuf recv,
                                            PerfMultiMetricHeader.Header header,
                                            ScheduleResult schedule,
                                            int msgSize,
                                            int runId,
                                            int phase,
                                            boolean allowSend,
                                            PerfCommon.LatencyReservoir reservoir) {
        long count = 0;
        for (int i = 0; i < pollSet.size(); i++) {
            Socket socket = pollSet.socket(i);
            int revents = pollSet.revents(i);
            if ((revents & PollEventType.POLLIN.getValue()) != 0) {
                while (true) {
                    int n = receiveNonBlocking(socket, recv);
                    if (n <= 0) {
                        break;
                    }
                    if (schedule != null) {
                        schedule.progressed = true;
                    }
                    awaitingReply[i] = false;
                    if (header != null
                        && reservoir != null
                        && n >= HEADER_BYTES
                        && PerfMultiMetricHeader.decodePayloadHeader(recv, n, header)
                        && header.runId == runId
                        && header.phase == phase) {
                        long nowUs = PerfMultiMetricHeader.nowUs();
                        reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
                        count++;
                    }
                }
            }
            if ((revents & PollEventType.POLLOUT.getValue()) != 0
                && pendingSends[i].pending) {
                if (tryFlushPending(pollSet, socket, i, pendingSends[i],
                        awaitingReply)
                    && schedule != null) {
                    schedule.progressed = true;
                }
            }
            if (allowSend
                && !awaitingReply[i]
                && !pendingSends[i].pending) {
                if (tryStartSend(pollSet, socket, i, pendingSends[i],
                        awaitingReply, msgSize, runId, phase, schedule.nextSeq)
                    && schedule != null) {
                    schedule.progressed = true;
                    schedule.nextSeq++;
                }
            }
        }
        return count;
    }

    private static void sendImmediate(Socket socket, byte[] payload) {
        int written = socket.send(payload, 0, payload.length, SendFlag.NONE);
        if (written != payload.length) {
            throw new IllegalStateException("send_failed");
        }
    }

    private static boolean trySendNonBlocking(Socket socket, PendingSend pending) {
        return socket.trySend(pending.payload, SendFlag.DONTWAIT);
    }

    private static int receiveNonBlocking(Socket socket, ByteBuf buffer) {
        buffer.clear();
        int rc = socket.tryRecv(buffer, ReceiveFlag.DONTWAIT);
        if (rc <= 0) {
            return 0;
        }
        buffer.readerIndex(0);
        return rc;
    }

    private static boolean awaitPendingRepliesDrained(List<Socket> activeSockets,
                                                      PendingSend[] pendingSends,
                                                      boolean[] awaitingReply,
                                                      SocketPollSet pollSet,
                                                      ByteBuf recv,
                                                      int pollTimeoutMs,
                                                      int connectTimeoutMs,
                                                      int settleMs) {
        if (!hasPendingReplies(awaitingReply)) {
            return true;
        }

        long deadlineNs = System.nanoTime()
            + (long) Math.max(connectTimeoutMs, Math.max(100, settleMs))
            * 1_000_000L;
        while (hasPendingReplies(awaitingReply)) {
            if (System.nanoTime() >= deadlineNs) {
                return false;
            }
            int readyCount = pollSet.poll(Math.max(1, pollTimeoutMs));
            if (readyCount <= 0) {
                continue;
            }
            processReadySockets(pollSet, pendingSends, awaitingReply, recv,
                null, null, 0, 0, 0, false);
        }
        for (int i = 0; i < activeSockets.size(); i++) {
            if (!awaitingReply[i] && !pendingSends[i].pending) {
                pollSet.setEvents(i, PollEventType.POLLIN.getValue());
            }
        }
        return true;
    }

    private static boolean hasPendingReplies(boolean[] awaitingReply) {
        for (boolean awaiting : awaitingReply) {
            if (awaiting) {
                return true;
            }
        }
        return false;
    }

    private static void releasePending(PendingSend[] pendingSends) {
        if (pendingSends == null) {
            return;
        }
        for (PendingSend pending : pendingSends) {
            if (pending != null) {
                pending.release();
            }
        }
    }

    private static void closeAll(List<? extends AutoCloseable> resources) {
        for (AutoCloseable resource : resources) {
            if (resource == null) {
                continue;
            }
            try {
                resource.close();
            } catch (Exception ignored) {
            }
        }
    }
}
