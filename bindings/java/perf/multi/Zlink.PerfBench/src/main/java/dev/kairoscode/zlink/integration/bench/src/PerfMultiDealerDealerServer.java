/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
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
 * DEALER(bind) receives one-way payloads with blocking recv + same-iteration
 * nonblocking drain.
 */
public final class PerfMultiDealerDealerServer {
    private static final String PATTERN = "MULTI_DEALER_DEALER";
    private static final int HEADER_BYTES = 32;
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
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
        int clientCount = PerfMultiCommon.resolveClients(PATTERN);

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.DEALER);
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

            try (Message payload = new Message()) {
                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(
                        PerfMultiCommon.resolveLatencySampleCap());
                PerfMultiMetricHeader.Header header =
                    new PerfMultiMetricHeader.Header();

                int activeRunId = 1;
                if (!runReceivePhase(server, payload, msgSize, activeRunId,
                        PerfMultiMetricHeader.PHASE_WARMUP, warmupSeconds,
                        false, false, header, reservoir)) {
                    return 2;
                }

                if (!runReceivePhase(server, payload, msgSize, activeRunId,
                        PerfMultiMetricHeader.PHASE_DRAIN, settleMs / 1000.0,
                        false, false, header, reservoir)) {
                    return 2;
                }

                ActiveStats active = new ActiveStats();
                if (!runReceivePhase(server, payload, msgSize, activeRunId,
                        PerfMultiMetricHeader.PHASE_ACTIVE, durationSeconds,
                        true, true, header, reservoir, active)) {
                    return 2;
                }
                if (active.count <= 0L) {
                    return 2;
                }

                PerfCommon.Stats stats = reservoir.snapshot();
                double configuredSeconds = Math.max(1, durationSeconds);
                double throughput = active.count / configuredSeconds;
                double fallbackLatencyUs =
                    (configuredSeconds * 1_000_000.0)
                    / Math.max(1.0, active.count);
                double meanUs = stats.meanUs() > 0.0
                    ? stats.meanUs() : fallbackLatencyUs;
                double p95Us = stats.p95Us() > 0.0 ? stats.p95Us() : meanUs;
                double p99Us = stats.p99Us() > 0.0 ? stats.p99Us() : p95Us;
                PerfCommon.printResult(PATTERN, transport, msgSize,
                    throughput, meanUs, p95Us, p99Us);
                return 0;
            }
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

    private static boolean waitConnectReadyCount(MonitorSocket monitor,
                                                 int expectedReady,
                                                 int timeoutMs) {
        return PerfMultiClientHelpers.waitConnectReadyCount(monitor,
            expectedReady, timeoutMs);
    }

    private static boolean runReceivePhase(Socket server, Message payload,
                                           int msgSize, int runId, int phase,
                                           double durationSeconds,
                                           boolean countMessage,
                                           boolean collectLatency,
                                           PerfMultiMetricHeader.Header header,
                                           PerfCommon.LatencyReservoir reservoir) {
        return runReceivePhase(server, payload, msgSize, runId, phase,
            durationSeconds, countMessage, collectLatency, header, reservoir,
            null);
    }

    private static boolean runReceivePhase(Socket server, Message payload,
                                           int msgSize, int runId, int phase,
                                           double durationSeconds,
                                           boolean countMessage,
                                           boolean collectLatency,
                                           PerfMultiMetricHeader.Header header,
                                           PerfCommon.LatencyReservoir reservoir,
                                           ActiveStats active) {
        if (durationSeconds <= 0.0) {
            return true;
        }

        long deadlineNs = System.nanoTime()
            + (long) (durationSeconds * 1_000_000_000L);
        while (System.nanoTime() < deadlineNs) {
            int n = receive(server, payload, ReceiveFlag.NONE);
            if (n <= 0) {
                continue;
            }
            if (!handleReceived(payload, n, msgSize, runId, phase, countMessage,
                    collectLatency, header, reservoir, active)) {
                return false;
            }
            if (!drainNonBlockingMessages(server, payload, msgSize, runId, phase,
                    countMessage, collectLatency, header, reservoir, active)) {
                return false;
            }
        }
        return true;
    }

    private static boolean drainNonBlockingMessages(Socket server,
                                                    Message payload,
                                                    int msgSize, int runId,
                                                    int phase,
                                                    boolean countMessage,
                                                    boolean collectLatency,
                                                    PerfMultiMetricHeader.Header header,
                                                    PerfCommon.LatencyReservoir reservoir,
                                                    ActiveStats active) {
        while (true) {
            int n = receive(server, payload, ReceiveFlag.DONTWAIT);
            if (n < 0) {
                return false;
            }
            if (n == 0) {
                return true;
            }
            if (!handleReceived(payload, n, msgSize, runId, phase, countMessage,
                    collectLatency, header, reservoir, active)) {
                return false;
            }
        }
    }

    private static boolean handleReceived(Message payload, int n,
                                          int msgSize,
                                          int runId, int phase,
                                          boolean countMessage,
                                          boolean collectLatency,
                                          PerfMultiMetricHeader.Header header,
                                          PerfCommon.LatencyReservoir reservoir,
                                          ActiveStats active) {
        if (n < HEADER_BYTES
            || !PerfMultiMetricHeader.decodePayloadHeader(payload, n, header)
            || header.runId != runId
            || header.phase != phase
            || header.msgSize != msgSize) {
            return true;
        }

        if (countMessage && active != null) {
            active.count++;
        }
        if (collectLatency) {
            long nowUs = PerfMultiMetricHeader.nowUs();
            if (header.sentTsUs > 0 && nowUs >= header.sentTsUs) {
                reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
            }
        }
        return true;
    }

    private static int receive(Socket socket, Message buffer,
                               ReceiveFlag flag) {
        if (flag == ReceiveFlag.DONTWAIT) {
            int rc = buffer.tryRecv(socket, flag);
            return rc < 0 ? 0 : rc;
        }
        while (true) {
            try {
                buffer.recv(socket, flag);
                return buffer.size();
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

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static final class ActiveStats {
        long count;
    }
}
