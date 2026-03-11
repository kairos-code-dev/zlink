/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.SocketType;
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
    private static final int HEADER_BYTES = PerfMultiMetricHeader.HEADER_SIZE;
    private static final long NANOS_PER_MILLISECOND = 1_000_000L;
    private static final long NANOS_PER_SECOND = 1_000_000_000L;

    private static final class ClientConfig {
        final int clients;
        final int warmupSeconds;
        final int durationSeconds;
        final int settleMs;
        final int connectTimeoutMs;
        final int pollTimeoutMs;

        ClientConfig(int clients, int warmupSeconds, int durationSeconds,
                     int settleMs, int connectTimeoutMs,
                     int pollTimeoutMs) {
            this.clients = clients;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
            this.connectTimeoutMs = connectTimeoutMs;
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

        ClientConfig config = new ClientConfig(
            PerfMultiCommon.resolveClients(PATTERN),
            PerfMultiCommon.resolveWarmupSeconds(),
            PerfMultiCommon.resolveDurationSeconds(),
            PerfMultiCommon.resolveSettleMs(),
            PerfMultiCommon.resolveConnectReadyTimeoutMs(),
            Math.max(0, PerfMultiCommon.resolveClientPollTimeoutMs()));

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<Socket> sockets = new ArrayList<>(config.clients);
            List<MonitorSocket> monitors = new ArrayList<>(config.clients);

            try {
                for (int i = 0; i < config.clients; i++) {
                    Socket socket = new Socket(context, CLIENT_SOCKET_TYPE);
                    applySocketOptions(socket);
                    socket.setOption(SocketOptions.SUBSCRIBE, "");
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
                        config.connectTimeoutMs)) {
                    System.err.println("ERROR,MULTI_PUBSUB,client,no_sockets");
                    return 2;
                }
                if (sockets.isEmpty()) {
                    System.err.println("ERROR,MULTI_PUBSUB,client,no_sockets");
                    return 2;
                }

                try (SocketPollSet pollSet = SocketPollSet.fromSockets(sockets,
                         PollEventType.POLLIN.getValue());
                     Message recv = new Message()) {
                    PerfMultiMetricHeader.Header header =
                        new PerfMultiMetricHeader.Header();

                    runDrainPhase(pollSet, recv,
                        Math.max(0L, config.warmupSeconds) * NANOS_PER_SECOND,
                        config.pollTimeoutMs);
                    runDrainPhase(pollSet, recv,
                        Math.max(0L, config.settleMs) * NANOS_PER_MILLISECOND,
                        config.pollTimeoutMs);

                    ActiveResult active = runActive(pollSet, recv, header,
                        msgSize, config);
                    if (active.count <= 0) {
                        System.err.println("ERROR,MULTI_PUBSUB,client,no_active_frames");
                        return 2;
                    }

                    double throughput = active.count
                        / (double) Math.max(1, config.durationSeconds);
                    PerfCommon.printResult(PATTERN, transport, msgSize,
                        throughput, active.stats.meanUs(),
                        active.stats.p95Us(), active.stats.p99Us());
                    return 0;
                }
            } finally {
                closeAll(monitors);
                closeAll(sockets);
                try {
                    context.shutdown();
                } catch (RuntimeException ignored) {
                }
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

    private static void runDrainPhase(SocketPollSet pollSet,
                                      Message recv,
                                      long durationNs,
                                      int pollTimeoutMs) {
        if (durationNs <= 0L) {
            return;
        }
        long deadlineNs = System.nanoTime() + durationNs;
        while (System.nanoTime() < deadlineNs) {
            drainReadySockets(pollSet, recv, pollTimeoutMs);
        }
    }

    private static ActiveResult runActive(SocketPollSet pollSet,
                                          Message recv,
                                          PerfMultiMetricHeader.Header header,
                                          int msgSize,
                                          ClientConfig config) {
        PerfCommon.LatencyReservoir reservoir = new PerfCommon.LatencyReservoir(
            PerfMultiCommon.resolveLatencySampleCap());
        long durationNs = Math.max(1L, config.durationSeconds)
            * NANOS_PER_SECOND;
        long deadlineNs = System.nanoTime() + durationNs;
        long count = 0L;
        int activeRunId = 1;

        while (System.nanoTime() < deadlineNs) {
            int readyCount = pollSet.poll(config.pollTimeoutMs);
            long drained = 0L;
            for (int i = 0; i < pollSet.size(); i++) {
                if (!pollSet.isReady(i, PollEventType.POLLIN.getValue())) {
                    continue;
                }
                Socket socket = pollSet.socket(i);
                while (true) {
                    int n = recv.tryRecv(socket, ReceiveFlag.DONTWAIT);
                    if (n <= 0) {
                        break;
                    }
                    drained++;
                    if (!isActiveSample(recv, n, header, msgSize)) {
                        continue;
                    }
                    if (header.runId != activeRunId) {
                        continue;
                    }
                    reservoir.add(Math.max(0L,
                        PerfMultiMetricHeader.nowUs() - header.sentTsUs));
                    count++;
                }
            }
        }

        return new ActiveResult(count, reservoir.snapshot());
    }

    private static boolean isActiveSample(Message recv,
                                          int recvBytes,
                                          PerfMultiMetricHeader.Header header,
                                          int msgSize) {
        return recvBytes >= HEADER_BYTES
            && PerfMultiMetricHeader.decodePayloadHeader(recv, recvBytes, header)
            && header.phase == PerfMultiMetricHeader.PHASE_ACTIVE
            && header.msgSize == msgSize;
    }

    private static long drainReadySockets(SocketPollSet pollSet,
                                          Message recv,
                                          int pollTimeoutMs) {
        long drained = 0L;
        pollSet.poll(pollTimeoutMs);
        for (int i = 0; i < pollSet.size(); i++) {
            if (!pollSet.isReady(i, PollEventType.POLLIN.getValue())) {
                continue;
            }
            Socket socket = pollSet.socket(i);
            while (true) {
                int n = recv.tryRecv(socket, ReceiveFlag.DONTWAIT);
                if (n <= 0) {
                    break;
                }
                drained++;
            }
        }
        return drained;
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
