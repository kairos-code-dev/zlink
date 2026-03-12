/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
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

/**
 * MULTI_PUBSUB server benchmark.
 * PUB(bind) publishes stamped payloads for warmup/active windows.
 */
public final class PerfMultiPubSubServer {
    private static final String PATTERN = "MULTI_PUBSUB";
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

    private static final class ServerConfig {
        final int payloadSize;
        final int warmupSeconds;
        final int durationSeconds;
        final int settleMs;

        ServerConfig(int payloadSize, int warmupSeconds, int durationSeconds,
                     int settleMs) {
            this.payloadSize = payloadSize;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
        }
    }

    private PerfMultiPubSubServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        ServerConfig config = resolveConfig(msgSize);
        String endpoint = resolveServerEndpoint(transport, "multi-pubsub");
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
        int clientCount = PerfMultiCommon.resolveClients(PATTERN);

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.PUB);
             MonitorSocket monitor = server.monitorOpen(
                 PerfMultiCommon.CONNECT_MONITOR_EVENTS)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);
            if (!waitConnectReadyCount(monitor, clientCount, connectTimeoutMs)) {
                return 2;
            }

            byte[] payload = new byte[config.payloadSize];
            int runId = 1;
            long seq = 1;

            seq = runPublishPhase(server, payload, runId, msgSize, seq,
                config.warmupSeconds, Phase.WARMUP);
            runSettle(config);
            runPublishPhase(server, payload, runId, msgSize, seq,
                config.durationSeconds, Phase.ACTIVE);

            return 0;
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_PUBSUB,server,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
            return 2;
        }
    }

    private static ServerConfig resolveConfig(int msgSize) {
        return new ServerConfig(
            Math.max(msgSize, MIN_PAYLOAD_BYTES),
            PerfMultiCommon.resolveWarmupSeconds(),
            PerfMultiCommon.resolveDurationSeconds(),
            PerfMultiCommon.resolveSettleMs()
        );
    }

    private static void applySocketOptions(Socket socket) {
        socket.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        socket.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        socket.setOption(SocketOptions.XPUB_NODROP,
            PerfCommon.parsePositiveEnv("PERF_PUBSUB_XPUB_NODROP", 1) > 0 ? 1 : 0);
        socket.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        socket.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        socket.setOption(SocketOptions.LINGER, 0);
    }

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
    }

    private static boolean waitConnectReadyCount(MonitorSocket monitor,
                                                 int expectedReady,
                                                 int timeoutMs) {
        return PerfMultiClientHelpers.waitConnectReadyCount(monitor,
            expectedReady, timeoutMs);
    }

    private static long runPublishPhase(Socket socket, byte[] payload, int runId,
                                        int msgSize, long seqStart,
                                        int durationSeconds, Phase phase) {
        long seq = seqStart;
        long deadline = System.nanoTime()
            + (long) Math.max(phase == Phase.WARMUP ? 0 : 1, durationSeconds)
            * NANOSECONDS_PER_SECOND;
        while (System.nanoTime() < deadline) {
            PerfMultiMetricHeader.stampPayload(payload, runId, phase.metricCode,
                msgSize, seq++, PerfMultiMetricHeader.nowUs());
            tryPublish(socket, payload);
        }
        return seq;
    }

    private static void runSettle(ServerConfig config) {
        if (config.settleMs > 0) {
            PerfCommon.sleepMillis(config.settleMs);
        }
    }

    private static boolean tryPublish(Socket socket, byte[] payload) {
        return socket.trySend(payload, 0, payload.length, SendFlag.DONTWAIT);
    }
}
