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

/**
 * MULTI_DEALER_DEALER server benchmark.
 * DEALER(bind) receives one-way payloads and reports server-side metrics.
 */
public final class PerfMultiDealerDealerServer {
    private static final String PATTERN = "MULTI_DEALER_DEALER";
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final int STARTUP_GRACE_SECONDS = 20;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;
    private static final long ACTIVE_IDLE_GRACE_NS = 750_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfMultiDealerDealerServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        String endpoint = resolveServerEndpoint(transport, "multi-dealer-dealer");
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.DEALER)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);

            byte[] payload = new byte[resolvePayloadSize(msgSize)];
            PerfCommon.LatencyReservoir reservoir =
                new PerfCommon.LatencyReservoir(
                    PerfMultiCommon.resolveLatencySampleCap());
            PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();

            long activeCount = 0;
            long activeStartNs = 0;
            long activeEndNs = 0;
            int activeRunId = -1;
            long serverStartNs = System.nanoTime();
            long startupDeadlineNs = serverStartNs
                + ((long) Math.max(0, warmupSeconds)
                + (long) Math.max(1, durationSeconds)
                + (long) STARTUP_GRACE_SECONDS)
                * NANOSECONDS_PER_SECOND
                + (long) Math.max(0, settleMs) * 1_000_000L;
            long lastReceiveNs = serverStartNs;
            long activeDurationNs =
                (long) Math.max(1, durationSeconds) * NANOSECONDS_PER_SECOND;

            while (true) {
                int n = receiveOnce(server, payload, ReceiveFlag.NONE);
                long nowNs = System.nanoTime();
                if (n <= 0) {
                    if (activeStartNs == 0) {
                        if (nowNs >= startupDeadlineNs) {
                            break;
                        }
                    } else if (nowNs >= activeEndNs
                        && nowNs - lastReceiveNs >= ACTIVE_IDLE_GRACE_NS) {
                        break;
                    }
                    continue;
                }
                lastReceiveNs = nowNs;
                if (n < HEADER_BYTES
                    || !PerfMultiMetricHeader.decodePayloadHeader(payload,
                    header)) {
                    continue;
                }
                if (header.msgSize != msgSize) {
                    continue;
                }
                if (activeRunId < 0) {
                    activeRunId = header.runId;
                }
                if (header.runId != activeRunId) {
                    continue;
                }
                if (header.phase != PerfMultiMetricHeader.PHASE_ACTIVE) {
                    continue;
                }
                if (activeStartNs == 0) {
                    activeStartNs = nowNs;
                    activeEndNs = activeStartNs + activeDurationNs;
                }
                if (nowNs > activeEndNs) {
                    continue;
                }
                long nowUs = PerfMultiMetricHeader.nowUs();
                reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
                activeCount++;
            }

            if (activeCount <= 0) {
                return 2;
            }
            double throughput = activeCount / (double) Math.max(1, durationSeconds);
            PerfCommon.Stats stats = reservoir.snapshot();
            PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                stats.meanUs(), stats.p95Us(), stats.p99Us());
            return 0;
        } catch (RuntimeException ignored) {
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

    private static int receiveOnce(Socket socket, byte[] buffer,
                                   ReceiveFlag flags) {
        ReceiveFlag op = toDontWaitReceiveFlag(flags);
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, op);
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isWouldBlock(errno)) {
                    return 0;
                }
                throw ex;
            }
        }
    }

    private static ReceiveFlag toDontWaitReceiveFlag(ReceiveFlag flag) {
        return flag == null || flag == ReceiveFlag.NONE
            ? ReceiveFlag.DONTWAIT
            : flag;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static int resolvePayloadSize(int msgSize) {
        return Math.max(msgSize, MIN_PAYLOAD_BYTES);
    }
}
