/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
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
        final int connectSettleMs;

        ServerConfig(int payloadSize, int warmupSeconds, int durationSeconds,
                     int settleMs, int connectSettleMs) {
            this.payloadSize = payloadSize;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
            this.connectSettleMs = connectSettleMs;
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

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.PUB)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);
            if (config.connectSettleMs > 0) {
                PerfCommon.sleepMillis(config.connectSettleMs);
            }

            byte[] payload = new byte[config.payloadSize];
            int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
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
            PerfMultiCommon.resolveSettleMs(),
            PerfCommon.parseNonNegativeEnv("PERF_PUBSUB_CONNECT_SETTLE_MS", 2000)
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

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
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
            sendOrThrow(socket, payload);
        }
        return seq;
    }

    private static void runSettle(ServerConfig config) {
        if (config.settleMs > 0) {
            PerfCommon.sleepMillis(config.settleMs);
        }
    }

    private static void sendOrThrow(Socket socket, byte[] payload) {
        int written = socket.send(payload, 0, payload.length, SendFlag.NONE);
        if (written <= 0) {
            throw new IllegalStateException("send_failed");
        }
    }
}
