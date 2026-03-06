/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
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
 * MULTI_PUBSUB client benchmark.
 * SUB(connect) receives one-way stamped payloads and reports metrics.
 */
public final class PerfMultiPubSubClient {
    private static final String PATTERN = "MULTI_PUBSUB";
    private static final SocketType CLIENT_SOCKET_TYPE = SocketType.SUB;
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfMultiPubSubClient() {
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
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);
        int connectSettleMs = PerfCommon.parseNonNegativeEnv(
            "PERF_PUBSUB_CONNECT_SETTLE_MS", 2000);

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(clients);

            try {
                for (int i = 0; i < clients; i++) {
                    Socket socket = new Socket(context, CLIENT_SOCKET_TYPE);
                    applySocketOptions(socket);
                    socket.setOption(SocketOptions.SUBSCRIBE, "");
                    PerfMultiTls.configureTlsClientIfNeeded(socket, transport);
                    socket.connect(endpoint);
                    sockets.add(socket);
                }

                if (connectSettleMs > 0) {
                    PerfCommon.sleepMillis(connectSettleMs);
                }
                if (sockets.isEmpty()) {
                    System.err.println("ERROR,MULTI_PUBSUB,client,no_sockets");
                    return 2;
                }

                byte[] recv = new byte[payloadSize];
                PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();

                // --- Warmup ---
                long warmupDeadline = System.nanoTime()
                    + (long) Math.max(0, warmupSeconds)
                    * NANOSECONDS_PER_SECOND;
                while (System.nanoTime() < warmupDeadline) {
                    long drained = drainWindow(sockets, recv);
                    if (drained <= 0) {
                        Thread.onSpinWait();
                    }
                }

                // --- Settle ---
                if (settleMs > 0) {
                    long settleDeadline = System.nanoTime()
                        + (long) settleMs * 1_000_000L;
                    while (System.nanoTime() < settleDeadline) {
                        long drained = drainWindow(sockets, recv);
                        if (drained <= 0) {
                            Thread.onSpinWait();
                        }
                    }
                }

                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(
                        PerfMultiCommon.resolveLatencySampleCap());

                long count = 0;
                long benchDeadline = System.nanoTime()
                    + (long) Math.max(1, durationSeconds)
                    * NANOSECONDS_PER_SECOND;
                int activeRunId = -1;

                // --- Active measurement ---
                while (System.nanoTime() < benchDeadline) {
                    boolean progressed = false;
                    for (Socket socket : sockets) {
                        while (true) {
                            int n = tryReceive(socket, recv);
                            if (n <= 0) {
                                break;
                            }
                            progressed = true;
                            if (n < HEADER_BYTES
                                || !PerfMultiMetricHeader.decodePayloadHeader(recv,
                                header)
                                || header.phase
                                != PerfMultiMetricHeader.PHASE_ACTIVE
                                || header.msgSize != msgSize) {
                                continue;
                            }
                            if (activeRunId < 0) {
                                activeRunId = header.runId;
                            }
                            if (header.runId != activeRunId) {
                                continue;
                            }
                            long nowUs = PerfMultiMetricHeader.nowUs();
                            reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
                            count++;
                        }
                    }
                    if (!progressed) {
                        Thread.onSpinWait();
                    }
                }

                if (count <= 0) {
                    System.err.println("ERROR,MULTI_PUBSUB,client,no_active_frames");
                    return 2;
                }
                double throughput = count / (double) Math.max(1, durationSeconds);
                PerfCommon.Stats stats = reservoir.snapshot();
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            } finally {
                closeAll(sockets);
            }
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_PUBSUB,client,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
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
        socket.setOption(SocketOptions.LINGER, 0);
    }

    private static int tryReceive(Socket socket, byte[] buffer) {
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

    private static long drainWindow(List<Socket> sockets, byte[] buffer) {
        long drainedCount = 0;
        for (Socket socket : sockets) {
            while (true) {
                int n = tryReceive(socket, buffer);
                if (n <= 0) {
                    break;
                }
                drainedCount++;
            }
        }
        return drainedCount;
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
