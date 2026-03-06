/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfSingleMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfTls;

/**
 * DEALER_DEALER one-way benchmark.
 * sender=DEALER(connect), receiver=DEALER(bind), header-stamped payload.
 */
public final class PerfDealerDealer {
    private static final String PATTERN = "DEALER_DEALER";

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfDealerDealer() {
    }

    public static int run(String transport, int msgSize) {
        try (Context context = new Context();
             Socket receiver = new Socket(context, SocketType.DEALER);
             Socket sender = new Socket(context, SocketType.DEALER)) {
            PerfCommon.applySingleContextOptions(context);
            PerfCommon.applySingleSocketOptions(receiver);
            PerfCommon.applySingleSocketOptions(sender);

            PerfTls.configureTlsServerIfNeeded(receiver, transport);
            PerfTls.configureTlsClientIfNeeded(sender, transport);

            String endpoint = PerfCommon.endpointFor(transport, "dealer-dealer");
            receiver.bind(endpoint);
            sender.connect(endpoint);
            Thread.sleep(300);

            int payloadSize = Math.max(msgSize, PerfSingleMetricHeader.HEADER_SIZE);
            byte[] payload = new byte[payloadSize];
            byte[] recv = new byte[payloadSize];

            int runId = PerfCommon.randomRunId();
            long seq = 1;

            int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
            for (int i = 0; i < warmupCount; i++) {
                PerfSingleMetricHeader.stampPayload(payload, runId,
                    PerfSingleMetricHeader.PHASE_WARMUP, msgSize, seq++,
                    PerfSingleMetricHeader.nowUs());
                sendBlocking(sender, payload, SendFlag.NONE);
                receiveBlocking(receiver, recv);
                while (receiveNonBlocking(receiver, recv) > 0) {
                }
            }

            Thread.sleep(PerfCommon.resolveSettleMs());

            PerfSingleMetricHeader.Header header = new PerfSingleMetricHeader.Header();
            PerfCommon.LatencyReservoir latency =
                new PerfCommon.LatencyReservoir(PerfCommon.resolveLatencySampleCap());

            long received = 0;
            long endNs = System.nanoTime()
                + (long) PerfCommon.resolveDurationSeconds() * 1_000_000_000L;
            long beginNs = System.nanoTime();

            while (System.nanoTime() < endNs) {
                PerfSingleMetricHeader.stampPayload(payload, runId,
                    PerfSingleMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                    PerfSingleMetricHeader.nowUs());
                sendBlocking(sender, payload, SendFlag.NONE);

                int n = receiveBlocking(receiver, recv);
                if (n > 0) {
                    received += collectActiveSample(recv, n, runId, header, latency);
                    while (true) {
                        int drained = receiveNonBlocking(receiver, recv);
                        if (drained <= 0) {
                            break;
                        }
                        received += collectActiveSample(recv, drained, runId,
                            header, latency);
                    }
                }
            }

            double elapsedSec = Math.max(1e-9,
                (System.nanoTime() - beginNs) / 1_000_000_000.0);
            double throughput = received / elapsedSec;
            PerfCommon.Stats stats = latency.snapshot();
            PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                stats.meanUs(), stats.p95Us(), stats.p99Us());
            return 0;
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return 2;
        } catch (RuntimeException ex) {
            return 2;
        }
    }

    private static long collectActiveSample(byte[] recv, int recvBytes, int runId,
                                            PerfSingleMetricHeader.Header header,
                                            PerfCommon.LatencyReservoir latency) {
        if (recvBytes < PerfSingleMetricHeader.HEADER_SIZE
            || !PerfSingleMetricHeader.decodePayloadHeader(recv, header)
            || header.runId != runId
            || header.phase != PerfSingleMetricHeader.PHASE_ACTIVE) {
            return 0L;
        }
        long nowUs = PerfSingleMetricHeader.nowUs();
        latency.add(Math.max(0L, nowUs - header.sentTsUs));
        return 1L;
    }

    private static int receiveBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.NONE);
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

    private static void sendBlocking(Socket socket, byte[] payload,
                                     SendFlag flags) {
        SendFlag op = flags == null ? SendFlag.NONE : flags;
        int written = socket.send(payload, 0, payload.length, op);
        if (written <= 0) {
            throw new IllegalStateException("send_failed");
        }
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }
}
