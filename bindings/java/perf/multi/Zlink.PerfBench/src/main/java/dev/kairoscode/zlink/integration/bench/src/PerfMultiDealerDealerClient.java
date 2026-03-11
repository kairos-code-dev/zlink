/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
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
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_DEALER_DEALER client benchmark.
 * DEALER(connect) sends one-way stamped payloads only.
 */
public final class PerfMultiDealerDealerClient {
    private static final String PATTERN = "MULTI_DEALER_DEALER";
    private static final SocketType CLIENT_SOCKET_TYPE = SocketType.DEALER;
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int SEND_BACKOFF_POLL_TIMEOUT_MS = 50;
    private static final long NANOS_PER_SECOND = 1_000_000_000L;
    private static final long NANOS_PER_MILLISECOND = 1_000_000L;

    private enum Phase {
        WARMUP(PerfMultiMetricHeader.PHASE_WARMUP),
        DRAIN(PerfMultiMetricHeader.PHASE_DRAIN),
        ACTIVE(PerfMultiMetricHeader.PHASE_ACTIVE);

        final int metricCode;

        Phase(int metricCode) {
            this.metricCode = metricCode;
        }
    }

    private static final class PendingSend {
        boolean pending;
        final ByteBuf payload;

        PendingSend(int payloadSize) {
            payload = Unpooled.directBuffer(payloadSize, payloadSize);
            payload.writeZero(payloadSize);
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

    private static final class SendPhaseResult {
        final long nextSeq;
        final int nextIndex;
        final boolean anySent;

        SendPhaseResult(long nextSeq, int nextIndex, boolean anySent) {
            this.nextSeq = nextSeq;
            this.nextIndex = nextIndex;
            this.anySent = anySent;
        }
    }

    private PerfMultiDealerDealerClient() {
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
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);
        int pollTimeoutMs = Math.max(0, PerfMultiCommon.resolveClientPollTimeoutMs());

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(clients);
            List<MonitorSocket> monitors = new ArrayList<>(clients);
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
                    System.err.println("ERROR,MULTI_DEALER_DEALER,client,no_active_sockets");
                    return 2;
                }
                closeAll(monitors);
                monitors.clear();
                if (sockets.size() != clients) {
                    System.err.println("ERROR,MULTI_DEALER_DEALER,client,no_active_sockets");
                    return 2;
                }

                pendingSends = createPendingSends(sockets.size(), payloadSize);
                int runId = 1;

                try (SocketPollSet pollSet = SocketPollSet.fromSockets(sockets, 0)) {
                    SendPhaseResult warmup = runSendPhase(sockets, pendingSends,
                        pollSet, runId, msgSize, 1L, warmupSeconds * NANOS_PER_SECOND,
                        Phase.WARMUP, 0, pollTimeoutMs, true);

                    SendPhaseResult drain = runSendPhase(sockets, pendingSends,
                        pollSet, runId, msgSize, warmup.nextSeq,
                        Math.max(0L, (long) settleMs) * NANOS_PER_MILLISECOND,
                        Phase.DRAIN, warmup.nextIndex, pollTimeoutMs, false);

                    SendPhaseResult active = runSendPhase(sockets, pendingSends,
                        pollSet, runId, msgSize, drain.nextSeq,
                        Math.max(1L, durationSeconds) * NANOS_PER_SECOND,
                        Phase.ACTIVE, drain.nextIndex, pollTimeoutMs, true);
                    if (!active.anySent) {
                        System.err.println(
                            "ERROR,MULTI_DEALER_DEALER,client,no_active_send");
                        return 2;
                    }
                }
                return 0;
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
            System.err.println("ERROR,MULTI_DEALER_DEALER,client,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
            return 2;
        }
    }

    private static SendPhaseResult runSendPhase(List<Socket> sockets,
                                                PendingSend[] pendingSends,
                                                SocketPollSet pollSet,
                                                int runId,
                                                int msgSize,
                                                long seqStart,
                                                long durationNs,
                                                Phase phase,
                                                int indexStart,
                                                int pollTimeoutMs,
                                                boolean sendActive) {
        if (durationNs <= 0L) {
            return new SendPhaseResult(seqStart, indexStart, false);
        }

        long deadlineNs = System.nanoTime() + durationNs;
        long seq = seqStart;
        int index = indexStart;
        boolean anySent = false;
        int socketCount = sockets.size();

        while (System.nanoTime() < deadlineNs) {
            boolean havePollout = false;
            boolean progressed = false;

            if (sendActive) {
                int startIndex = index;
                for (int attempt = 0; attempt < socketCount; attempt++) {
                    int socketIndex = (startIndex + attempt) % socketCount;
                    PendingSend pending = pendingSends[socketIndex];
                    if (pending.pending
                        && !pollSet.isReady(socketIndex,
                            PollEventType.POLLOUT.getValue())) {
                        continue;
                    }

                    SendStatus status = trySendOneMessage(
                        sockets.get(socketIndex), pending, runId,
                        phase.metricCode, msgSize, seq);
                    if (status == SendStatus.OK) {
                        seq++;
                        progressed = true;
                        anySent = true;
                        pending.pending = false;
                    } else if (status == SendStatus.BLOCKED) {
                        pending.pending = true;
                    } else {
                        return new SendPhaseResult(seq, index, anySent);
                    }
                }
                index = socketCount == 0 ? 0 : (startIndex + 1) % socketCount;
            } else {
                for (PendingSend pending : pendingSends) {
                    pending.pending = false;
                }
            }

            for (int i = 0; i < socketCount; i++) {
                int events = pendingSends[i].pending
                    ? PollEventType.POLLOUT.getValue() : 0;
                pollSet.setEvents(i, events);
                if (events != 0) {
                    havePollout = true;
                }
            }

            if (!havePollout) {
                if (progressed || sendActive) {
                    continue;
                }
                int idleTimeoutMs = resolveIdlePollTimeoutMs(deadlineNs);
                if (idleTimeoutMs > 0) {
                    PerfCommon.sleepMillis(idleTimeoutMs);
                }
                continue;
            }

            int timeoutMs = resolveSendPollTimeoutMs(progressed, deadlineNs,
                pollTimeoutMs);
            pollSet.poll(timeoutMs);
        }

        return new SendPhaseResult(seq, index, anySent);
    }

    private enum SendStatus {
        OK,
        BLOCKED,
        FATAL
    }

    private static SendStatus trySendOneMessage(Socket socket,
                                                PendingSend pending,
                                                int runId,
                                                int phase,
                                                int msgSize,
                                                long seq) {
        pending.reset();
        if (!PerfMultiMetricHeader.stampPayload(pending.payload, runId, phase,
                msgSize, seq, PerfMultiMetricHeader.nowUs())) {
            return SendStatus.FATAL;
        }
        return socket.trySend(pending.payload, SendFlag.DONTWAIT)
            ? SendStatus.OK : SendStatus.BLOCKED;
    }

    private static int resolveIdlePollTimeoutMs(long deadlineNs) {
        long remainNs = deadlineNs - System.nanoTime();
        if (remainNs <= 0L) {
            return 0;
        }
        long remainMs = remainNs / NANOS_PER_MILLISECOND;
        return (int) Math.min(SEND_BACKOFF_POLL_TIMEOUT_MS,
            Math.max(0L, remainMs));
    }

    private static int resolveSendPollTimeoutMs(boolean progressed,
                                                long deadlineNs,
                                                int pollTimeoutMs) {
        if (progressed) {
            return 0;
        }
        long remainNs = deadlineNs - System.nanoTime();
        if (remainNs <= 0L) {
            return 0;
        }
        long remainMs = remainNs / NANOS_PER_MILLISECOND;
        int baseTimeoutMs = pollTimeoutMs > 0 ? pollTimeoutMs
            : SEND_BACKOFF_POLL_TIMEOUT_MS;
        return (int) Math.min(baseTimeoutMs, Math.max(0L, remainMs));
    }

    private static PendingSend[] createPendingSends(int count, int payloadSize) {
        PendingSend[] pendingSends = new PendingSend[count];
        for (int i = 0; i < count; i++) {
            pendingSends[i] = new PendingSend(payloadSize);
        }
        return pendingSends;
    }

    private static void applySocketOptions(Socket socket) {
        socket.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        socket.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        socket.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        socket.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        socket.setOption(SocketOptions.LINGER, 0);
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
