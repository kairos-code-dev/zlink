/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
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

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class PendingSend {
        boolean pending;
        final byte[] payload;

        PendingSend(int payloadSize) {
            this.payload = new byte[payloadSize];
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
        int pollTimeoutMs = PerfMultiCommon.resolveClientPollTimeoutMs();
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(clients);
            List<MonitorSocket> monitors = new ArrayList<>(clients);
            List<Socket> activeSockets = new ArrayList<>(clients);

            try {
                for (int i = 0; i < clients; i++) {
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
                        connectTimeoutMs,
                        true)) {
                        activeSockets.add(sockets.get(i));
                    }
                }
                if (activeSockets.isEmpty()) {
                    return 2;
                }

                Poller poller = new Poller();
                for (int i = 0; i < activeSockets.size(); i++) {
                    poller.add(activeSockets.get(i), PollEventType.POLLIN.getValue(),
                        Integer.valueOf(i));
                }

                byte[] recv = new byte[payloadSize];
                PendingSend[] pendingSends = new PendingSend[activeSockets.size()];
                for (int i = 0; i < pendingSends.length; i++) {
                    pendingSends[i] = new PendingSend(payloadSize);
                }
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
                long seq = 1;
                int index = 0;
                boolean[] awaitingReply = new boolean[activeSockets.size()];

                // --- Warmup ---
                long warmupDeadline = System.nanoTime()
                    + (long) Math.max(0, warmupSeconds) * NANOSECONDS_PER_SECOND;
                while (System.nanoTime() < warmupDeadline) {
                    boolean progressed = false;
                    Socket socket = activeSockets.get(index);
                    PendingSend pending = pendingSends[index];
                    if (!awaitingReply[index] && !pending.pending) {
                        PerfMultiMetricHeader.stampPayload(pending.payload, runId,
                            PerfMultiMetricHeader.PHASE_WARMUP, msgSize, seq++,
                            PerfMultiMetricHeader.nowUs());
                        progressed = tryStartSend(poller, socket, index, pending,
                            awaitingReply);
                    }
                    index = (index + 1) % activeSockets.size();

                    int eventCount = poller.pollCount(pollTimeoutMs);
                    for (int i = 0; i < eventCount; i++) {
                        Socket readySocket = poller.readySocket(i);
                        if (readySocket == null) {
                            continue;
                        }
                        int socketIndex = (Integer) poller.readyTag(i);
                        if ((poller.readyRevents(i) & PollEventType.POLLOUT.getValue()) != 0
                            && pendingSends[socketIndex].pending) {
                            progressed |= tryFlushPending(poller, readySocket,
                                socketIndex, pendingSends[socketIndex],
                                awaitingReply);
                        }
                        while (true) {
                            int n = receiveNonBlocking(readySocket, recv);
                            if (n <= 0) {
                                break;
                            }
                            progressed = true;
                            awaitingReply[socketIndex] = false;
                        }
                    }
                }

                // --- Settle ---
                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(
                        PerfMultiCommon.resolveLatencySampleCap());
                PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();

                long count = 0;
                long benchDeadline = System.nanoTime()
                    + (long) Math.max(1, durationSeconds) * NANOSECONDS_PER_SECOND;
                long benchBegin = System.nanoTime();

                // --- Active measurement ---
                while (System.nanoTime() < benchDeadline) {
                    boolean progressed = false;
                    Socket socket = activeSockets.get(index);
                    PendingSend pending = pendingSends[index];
                    if (!awaitingReply[index] && !pending.pending) {
                        PerfMultiMetricHeader.stampPayload(pending.payload, runId,
                            PerfMultiMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                            PerfMultiMetricHeader.nowUs());
                        progressed = tryStartSend(poller, socket, index, pending,
                            awaitingReply);
                    }
                    index = (index + 1) % activeSockets.size();

                    int eventCount = poller.pollCount(pollTimeoutMs);
                    for (int i = 0; i < eventCount; i++) {
                        Socket readySocket = poller.readySocket(i);
                        if (readySocket == null) {
                            continue;
                        }
                        int socketIndex = (Integer) poller.readyTag(i);
                        if ((poller.readyRevents(i) & PollEventType.POLLOUT.getValue()) != 0
                            && pendingSends[socketIndex].pending) {
                            progressed |= tryFlushPending(poller, readySocket,
                                socketIndex, pendingSends[socketIndex],
                                awaitingReply);
                        }
                        while (true) {
                            int n = receiveNonBlocking(readySocket, recv);
                            if (n <= 0) {
                                break;
                            }
                            progressed = true;
                            awaitingReply[socketIndex] = false;
                            if (n >= HEADER_BYTES
                                && PerfMultiMetricHeader.decodePayloadHeader(recv, header)
                                && header.runId == runId
                                && header.phase == PerfMultiMetricHeader.PHASE_ACTIVE) {
                                long nowUs = PerfMultiMetricHeader.nowUs();
                                reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
                                count++;
                            }
                        }
                    }
                }

                try {
                    sendImmediate(activeSockets.get(0), STOP_TOKEN);
                } catch (RuntimeException ignored) {
                }

                double elapsedSec = Math.max(1e-9,
                    (System.nanoTime() - benchBegin) / 1_000_000_000.0);
                double throughput = count / elapsedSec;
                PerfCommon.Stats stats = reservoir.snapshot();
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            } finally {
                closeAll(monitors);
                closeAll(sockets);
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
        socket.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        socket.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
    }

    private static boolean tryStartSend(Poller poller, Socket socket, int index,
                                        PendingSend pending,
                                        boolean[] awaitingReply) {
        if (trySendNonBlocking(socket, pending.payload)) {
            awaitingReply[index] = true;
            return true;
        }
        if (!pending.pending) {
            pending.pending = true;
            poller.modify(socket, PollEventType.POLLIN, PollEventType.POLLOUT);
        }
        return false;
    }

    private static boolean tryFlushPending(Poller poller, Socket socket,
                                           int index, PendingSend pending,
                                           boolean[] awaitingReply) {
        if (!pending.pending) {
            return false;
        }
        if (!trySendNonBlocking(socket, pending.payload)) {
            return false;
        }
        pending.pending = false;
        awaitingReply[index] = true;
        poller.modify(socket, PollEventType.POLLIN);
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
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    return false;
                }
                throw ex;
            }
        }
    }

    private static void sendImmediate(Socket socket, byte[] payload) {
        int written = socket.send(payload, 0, payload.length, SendFlag.NONE);
        if (written != payload.length) {
            throw new IllegalStateException("send_failed");
        }
    }

    private static int receiveNonBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.DONTWAIT);
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    return 0;
                }
                throw ex;
            }
        }
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
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
