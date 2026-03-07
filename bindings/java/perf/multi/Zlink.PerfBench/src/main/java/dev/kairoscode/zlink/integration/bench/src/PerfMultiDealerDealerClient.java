/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
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
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private enum Phase {
        WARMUP(PerfMultiMetricHeader.PHASE_WARMUP),
        ACTIVE(PerfMultiMetricHeader.PHASE_ACTIVE);

        final int metricCode;

        Phase(int metricCode) {
            this.metricCode = metricCode;
        }
    }

    private static final class ClientConfig {
        final int clients;
        final int warmupSeconds;
        final int durationSeconds;
        final int settleMs;
        final int connectTimeoutMs;
        final int payloadSize;
        final int pollTimeoutMs;

        ClientConfig(int clients, int warmupSeconds, int durationSeconds,
                     int settleMs, int connectTimeoutMs, int payloadSize,
                     int pollTimeoutMs) {
            this.clients = clients;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
            this.connectTimeoutMs = connectTimeoutMs;
            this.payloadSize = payloadSize;
            this.pollTimeoutMs = pollTimeoutMs;
        }
    }

    private static final class PendingSend {
        boolean pending;
        final byte[] payload;

        PendingSend(int payloadSize) {
            this.payload = new byte[payloadSize];
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

        ClientConfig config = resolveConfig(msgSize);

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(config.clients);
            List<MonitorSocket> monitors = new ArrayList<>(config.clients);
            List<Socket> activeSockets = new ArrayList<>(config.clients);

            try {
                for (int i = 0; i < config.clients; i++) {
                    Socket socket = new Socket(context, CLIENT_SOCKET_TYPE);
                    applySocketOptions(socket);
                    PerfMultiTls.configureTlsClientIfNeeded(socket, transport);
                    MonitorSocket monitor = socket.monitorOpen(
                        MonitorEventType.CONNECTION_READY.getValue()
                            | MonitorEventType.CONNECTED.getValue()
                            | MonitorEventType.ACCEPTED.getValue());
                    socket.connect(endpoint);
                    sockets.add(socket);
                    monitors.add(monitor);
                }

                for (int i = 0; i < monitors.size(); i++) {
                    if (PerfCommon.waitMonitorReady(monitors.get(i),
                        config.connectTimeoutMs,
                        true)) {
                        activeSockets.add(sockets.get(i));
                    }
                }
                if (activeSockets.isEmpty()) {
                    System.err.println("ERROR,MULTI_DEALER_DEALER,client,no_active_sockets");
                    return 2;
                }

                byte[] payload = new byte[config.payloadSize];
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);

                SendPhaseResult warmup = runSendPhase(activeSockets, payload,
                    runId, msgSize, 1L, config.warmupSeconds, Phase.WARMUP, 0,
                    config.pollTimeoutMs);
                if (config.settleMs > 0) {
                    PerfCommon.sleepMillis(config.settleMs);
                }

                SendPhaseResult active = runSendPhase(activeSockets, payload,
                    runId, msgSize, warmup.nextSeq,
                    Math.max(1, config.durationSeconds), Phase.ACTIVE,
                    warmup.nextIndex, config.pollTimeoutMs);
                if (!active.anySent) {
                    System.err.println("ERROR,MULTI_DEALER_DEALER,client,no_active_send");
                    return 2;
                }
                return 0;
            } finally {
                closeAll(monitors);
                closeAll(sockets);
            }
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_DEALER_DEALER,client,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
            return 2;
        }
    }

    private static ClientConfig resolveConfig(int msgSize) {
        return new ClientConfig(
            PerfMultiCommon.resolveClients(PATTERN),
            PerfMultiCommon.resolveWarmupSeconds(),
            PerfMultiCommon.resolveDurationSeconds(),
            PerfMultiCommon.resolveSettleMs(),
            PerfMultiCommon.resolveConnectReadyTimeoutMs(),
            Math.max(msgSize, MIN_PAYLOAD_BYTES),
            PerfMultiCommon.resolveClientPollTimeoutMs()
        );
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

    private static SendPhaseResult runSendPhase(List<Socket> sockets,
                                                byte[] payload,
                                                int runId, int msgSize,
                                                long seqStart,
                                                int durationSeconds,
                                                Phase phase,
                                                int indexStart,
                                                int pollTimeoutMs) {
        long seq = seqStart;
        int index = indexStart;
        boolean anySent = false;
        long deadline = System.nanoTime()
            + (long) Math.max(0, durationSeconds) * NANOSECONDS_PER_SECOND;
        int socketCount = sockets.size();
        PendingSend[] pendingSends = new PendingSend[socketCount];
        for (int i = 0; i < socketCount; i++) {
            pendingSends[i] = new PendingSend(payload.length);
        }

        try (Poller poller = new Poller()) {
            while (System.nanoTime() < deadline) {
                boolean progressed = false;
                Socket socket = sockets.get(index);
                PendingSend pending = pendingSends[index];
                if (!pending.pending) {
                    PerfMultiMetricHeader.stampPayload(pending.payload, runId,
                        phase.metricCode, msgSize, seq++,
                        PerfMultiMetricHeader.nowUs());
                    progressed = tryStartSend(poller, socket, index, pending);
                    anySent = anySent || progressed;
                }
                index = (index + 1) % socketCount;

                int eventCount = poller.pollCount(progressed ? 0 : pollTimeoutMs);
                for (int i = 0; i < eventCount; i++) {
                    Socket readySocket = poller.readySocket(i);
                    if (readySocket == null) {
                        continue;
                    }
                    int socketIndex = (Integer) poller.readyTag(i);
                    progressed |= tryFlushPending(poller, readySocket,
                        pendingSends[socketIndex]);
                    anySent = anySent || progressed;
                }
            }
        }

        return new SendPhaseResult(seq, index, anySent);
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

    private static boolean tryStartSend(Poller poller, Socket socket, int index,
                                        PendingSend pending) {
        if (trySendNonBlocking(socket, pending.payload)) {
            return true;
        }
        if (!pending.pending) {
            pending.pending = true;
            poller.add(socket, PollEventType.POLLOUT.getValue(),
                Integer.valueOf(index));
        }
        return false;
    }

    private static boolean tryFlushPending(Poller poller, Socket socket,
                                           PendingSend pending) {
        if (!pending.pending) {
            return false;
        }
        if (!trySendNonBlocking(socket, pending.payload)) {
            return false;
        }
        pending.pending = false;
        poller.remove(socket);
        return true;
    }

    private static boolean trySendNonBlocking(Socket socket, byte[] payload) {
        while (true) {
            try {
                int written = socket.send(payload, 0, payload.length,
                    SendFlag.DONTWAIT);
                if (written != payload.length) {
                    throw new IllegalStateException("send_failed");
                }
                return true;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (errno == 4) {
                    continue;
                }
                if (errno == 11 || errno == 10035) {
                    return false;
                }
                throw ex;
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
