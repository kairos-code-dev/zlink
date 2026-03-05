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
    private static final int RETRY_BACKOFF_MS = 1;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;
    private static final long DATA_SEND_RETRY_BUDGET_NS = 5_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

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

        int clients = PerfMultiCommon.resolveClients(PATTERN);
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
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

                byte[] payload = new byte[payloadSize];
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
                long seq = 1;
                int index = 0;

                // --- Warmup ---
                long warmupDeadline = System.nanoTime()
                    + (long) Math.max(0, warmupSeconds)
                    * NANOSECONDS_PER_SECOND;
                while (System.nanoTime() < warmupDeadline) {
                    Socket socket = activeSockets.get(index);
                    PerfMultiMetricHeader.stampPayload(payload, runId,
                        PerfMultiMetricHeader.PHASE_WARMUP, msgSize, seq++,
                        PerfMultiMetricHeader.nowUs());
                    sendRetry(socket, payload, SendFlag.NONE,
                        DATA_SEND_RETRY_BUDGET_NS);
                    index = (index + 1) % activeSockets.size();
                }

                // --- Settle ---
                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                // --- Active measurement ---
                long benchDeadline = System.nanoTime()
                    + (long) Math.max(1, durationSeconds)
                    * NANOSECONDS_PER_SECOND;
                while (System.nanoTime() < benchDeadline) {
                    Socket socket = activeSockets.get(index);
                    PerfMultiMetricHeader.stampPayload(payload, runId,
                        PerfMultiMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                        PerfMultiMetricHeader.nowUs());
                    sendRetry(socket, payload, SendFlag.NONE,
                        DATA_SEND_RETRY_BUDGET_NS);
                    index = (index + 1) % activeSockets.size();
                }
                return 0;
            } finally {
                closeAll(monitors);
                closeAll(sockets);
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

    private static int sendRetry(Socket socket, byte[] payload,
                                 SendFlag flags,
                                 long retryBudgetNs) {
        if (retryBudgetNs <= 0L) {
            return 0;
        }
        SendFlag op = toDontWaitSendFlag(flags);
        long retryDeadline = System.nanoTime() + retryBudgetNs;
        while (true) {
            try {
                return socket.send(payload, 0, payload.length, op);
            } catch (ZlinkException ex) {
                if (shouldRetry(ex)) {
                    if (System.nanoTime() >= retryDeadline) {
                        return 0;
                    }
                    continue;
                }
                throw ex;
            }
        }
    }

    private static SendFlag toDontWaitSendFlag(SendFlag flag) {
        return flag == null || flag == SendFlag.NONE
            ? SendFlag.DONTWAIT
            : flag;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean shouldRetry(ZlinkException ex) {
        int errno = ex.errno();
        if (isInterrupted(errno)) {
            return true;
        }
        if (isWouldBlock(errno)) {
            sleepMillis(RETRY_BACKOFF_MS);
            return true;
        }
        return false;
    }

    private static void sleepMillis(long millis) {
        try {
            Thread.sleep(Math.max(1L, millis));
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
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
