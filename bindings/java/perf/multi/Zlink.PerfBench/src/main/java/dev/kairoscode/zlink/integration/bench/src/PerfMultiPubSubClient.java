/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
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
    private static final long NANOS_PER_MILLISECOND = 1_000_000L;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private enum Phase {
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
        final int payloadSize;
        final int connectSettleMs;
        final int pollTimeoutMs;

        ClientConfig(int clients, int warmupSeconds, int durationSeconds,
                     int settleMs, int payloadSize, int connectSettleMs,
                     int pollTimeoutMs) {
            this.clients = clients;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
            this.payloadSize = payloadSize;
            this.connectSettleMs = connectSettleMs;
            this.pollTimeoutMs = pollTimeoutMs;
        }
    }

    private static final class ActiveResult {
        final long count;
        final PerfCommon.Stats stats;

        ActiveResult(long count, PerfCommon.Stats stats) {
            this.count = count;
            this.stats = stats;
        }
    }

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

        ClientConfig config = resolveConfig(msgSize);

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(config.clients);

            try {
                for (int i = 0; i < config.clients; i++) {
                    Socket socket = new Socket(context, CLIENT_SOCKET_TYPE);
                    applySocketOptions(socket);
                    socket.setOption(SocketOptions.SUBSCRIBE, "");
                    PerfMultiTls.configureTlsClientIfNeeded(socket, transport);
                    socket.connect(endpoint);
                    sockets.add(socket);
                }

                if (config.connectSettleMs > 0) {
                    PerfCommon.sleepMillis(config.connectSettleMs);
                }
                if (sockets.isEmpty()) {
                    System.err.println("ERROR,MULTI_PUBSUB,client,no_sockets");
                    return 2;
                }
                Poller poller = new Poller();
                for (Socket socket : sockets) {
                    poller.add(socket, PollEventType.POLLIN);
                }

                byte[] recv = new byte[config.payloadSize];
                PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();

                runWarmup(poller, recv, config);
                runSettle(poller, recv, config);
                ActiveResult active = runActive(poller, recv, header, msgSize,
                    config);

                if (active.count <= 0) {
                    System.err.println("ERROR,MULTI_PUBSUB,client,no_active_frames");
                    return 2;
                }
                double throughput = active.count
                    / (double) Math.max(1, config.durationSeconds);
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    active.stats.meanUs(), active.stats.p95Us(),
                    active.stats.p99Us());
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

    private static ClientConfig resolveConfig(int msgSize) {
        return new ClientConfig(
            PerfMultiCommon.resolveClients(PATTERN),
            PerfMultiCommon.resolveWarmupSeconds(),
            PerfMultiCommon.resolveDurationSeconds(),
            PerfMultiCommon.resolveSettleMs(),
            Math.max(msgSize, MIN_PAYLOAD_BYTES),
            PerfCommon.parseNonNegativeEnv("PERF_PUBSUB_CONNECT_SETTLE_MS", 2000),
            PerfMultiCommon.resolveClientPollTimeoutMs()
        );
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

    private static void runWarmup(Poller poller, byte[] recv,
                                  ClientConfig config) {
        long durationNs = (long) Math.max(0, config.warmupSeconds)
            * NANOSECONDS_PER_SECOND;
        runDrainPhase(poller, recv, durationNs, config.pollTimeoutMs);
    }

    private static void runSettle(Poller poller, byte[] recv,
                                  ClientConfig config) {
        long durationNs = (long) Math.max(0, config.settleMs)
            * NANOS_PER_MILLISECOND;
        runDrainPhase(poller, recv, durationNs, config.pollTimeoutMs);
    }

    private static void runDrainPhase(Poller poller, byte[] recv,
                                      long durationNs, int pollTimeoutMs) {
        if (durationNs <= 0L) {
            return;
        }
        long deadline = System.nanoTime() + durationNs;
        while (System.nanoTime() < deadline) {
            drainReadySockets(poller, recv, pollTimeoutMs);
        }
    }

    private static ActiveResult runActive(Poller poller, byte[] recv,
                                          PerfMultiMetricHeader.Header header,
                                          int msgSize, ClientConfig config) {
        PerfCommon.LatencyReservoir reservoir = new PerfCommon.LatencyReservoir(
            PerfMultiCommon.resolveLatencySampleCap());
        long deadline = System.nanoTime()
            + (long) Math.max(1, config.durationSeconds)
            * NANOSECONDS_PER_SECOND;
        long count = 0;
        int activeRunId = -1;

        while (System.nanoTime() < deadline) {
            int eventCount = poller.pollCount(config.pollTimeoutMs);
            for (int i = 0; i < eventCount; i++) {
                Socket socket = poller.readySocket(i);
                if (socket == null) {
                    continue;
                }
                while (true) {
                    int n = tryReceive(socket, recv);
                    if (n <= 0) {
                        break;
                    }
                    if (!isActiveSample(n, recv, header, msgSize)) {
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
        }

        return new ActiveResult(count, reservoir.snapshot());
    }

    private static boolean isActiveSample(int recvBytes, byte[] recv,
                                          PerfMultiMetricHeader.Header header,
                                          int msgSize) {
        return recvBytes >= HEADER_BYTES
            && PerfMultiMetricHeader.decodePayloadHeader(recv, header)
            && header.phase == Phase.ACTIVE.metricCode
            && header.msgSize == msgSize;
    }

    private static long drainReadySockets(Poller poller, byte[] buffer,
                                          int pollTimeoutMs) {
        long drainedCount = 0;
        int eventCount = poller.pollCount(pollTimeoutMs);
        for (int i = 0; i < eventCount; i++) {
            Socket socket = poller.readySocket(i);
            if (socket == null) {
                continue;
            }
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
