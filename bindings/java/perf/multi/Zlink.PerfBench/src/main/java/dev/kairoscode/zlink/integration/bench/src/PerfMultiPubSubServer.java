/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
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

    private static final int ERRNO_EINTR = 4;

    private PerfMultiPubSubServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int connectSettleMs = PerfCommon.parseNonNegativeEnv(
            "PERF_PUBSUB_CONNECT_SETTLE_MS", 2000);
        String endpoint = resolveServerEndpoint(transport, "multi-pubsub");

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.PUB)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);
            if (connectSettleMs > 0) {
                PerfCommon.sleepMillis(connectSettleMs);
            }

            byte[] payload = new byte[payloadSize];
            int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
            long seq = 1;

            // --- Warmup ---
            long warmupDeadline = System.nanoTime()
                + (long) Math.max(0, warmupSeconds) * NANOSECONDS_PER_SECOND;
            while (System.nanoTime() < warmupDeadline) {
                PerfMultiMetricHeader.stampPayload(payload, runId,
                    PerfMultiMetricHeader.PHASE_WARMUP, msgSize, seq++,
                    PerfMultiMetricHeader.nowUs());
                sendOrThrow(server, payload);
            }

            // --- Settle ---
            if (settleMs > 0) {
                PerfCommon.sleepMillis(settleMs);
            }

            // --- Active measurement ---
            long activeDeadline = System.nanoTime()
                + (long) Math.max(1, durationSeconds) * NANOSECONDS_PER_SECOND;
            while (System.nanoTime() < activeDeadline) {
                PerfMultiMetricHeader.stampPayload(payload, runId,
                    PerfMultiMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                    PerfMultiMetricHeader.nowUs());
                sendOrThrow(server, payload);
            }

            return 0;
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_PUBSUB,server,"
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

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
    }

    private static void sendOrThrow(Socket socket, byte[] payload) {
        while (true) {
            try {
                socket.send(payload, 0, payload.length, SendFlag.NONE);
                return;
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                throw ex;
            }
        }
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }
}
