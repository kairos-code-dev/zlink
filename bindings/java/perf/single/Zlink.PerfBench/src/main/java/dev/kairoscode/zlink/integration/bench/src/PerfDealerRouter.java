/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfSingleMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfTls;
import dev.kairoscode.zlink.options.SocketOptions;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.LongAdder;

/**
 * DEALER_ROUTER one-way benchmark.
 */
public final class PerfDealerRouter {
    private static final String PATTERN = "DEALER_ROUTER";

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int CONNECT_MONITOR_EVENTS =
        MonitorEventType.CONNECTION_READY.getValue()
            | MonitorEventType.CONNECTED.getValue()
            | MonitorEventType.ACCEPTED.getValue();

    private PerfDealerRouter() {
    }

    public static int run(String transport, int msgSize) {
        try (Context context = new Context();
             Socket router = new Socket(context, SocketType.ROUTER);
             Socket dealer = new Socket(context, SocketType.DEALER);
             MonitorSocket dealerMonitor = dealer.monitorOpen(CONNECT_MONITOR_EVENTS)) {
            PerfCommon.applySingleContextOptions(context);
            PerfCommon.applySingleSocketOptions(router);
            PerfCommon.applySingleSocketOptions(dealer);

            PerfTls.configureTlsServerIfNeeded(router, transport);
            PerfTls.configureTlsClientIfNeeded(dealer, transport);
            dealer.setOption(SocketOptions.ROUTING_ID, "CLIENT");

            String endpoint = PerfCommon.endpointFor(transport, "dealer-router");
            router.bind(endpoint);
            dealer.connect(endpoint);
            if (!PerfCommon.waitMonitorReady(dealerMonitor, 5000, true)) {
                return 2;
            }

            int payloadSize = Math.max(msgSize, PerfSingleMetricHeader.HEADER_SIZE);
            int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
            int durationSeconds = PerfCommon.resolveDurationSeconds();
            int latencyCap = PerfCommon.resolveLatencySampleCap();
            int recvTimeoutMs = PerfCommon.resolveRecvTimeoutMs();
            int runId = PerfCommon.randomRunId();

            try (Arena arena = Arena.ofConfined()) {
                MemorySegment payload = arena.allocate(payloadSize, 8);
                payload.fill((byte) 'a');
                long[] seq = new long[] {1L};

                PhaseResult warmup = runPhase(dealer, router, payload, payloadSize,
                    msgSize, runId, seq, PerfSingleMetricHeader.PHASE_WARMUP,
                    warmupCount, 0, 0, recvTimeoutMs);
                if (!warmup.ok || warmup.received < warmupCount) {
                    return 2;
                }

                Thread.sleep(PerfCommon.resolveSettleMs());

                PhaseResult active = runPhase(dealer, router, payload, payloadSize,
                    msgSize, runId, seq, PerfSingleMetricHeader.PHASE_ACTIVE,
                    0, durationSeconds, latencyCap, recvTimeoutMs);
                if (!active.ok || active.received <= 0) {
                    return 2;
                }

                double throughput =
                    active.received / (double) Math.max(durationSeconds, 1);
                PerfCommon.Stats stats = active.latency.snapshot();
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return 2;
        } catch (RuntimeException ex) {
            return 2;
        }
    }

    private static PhaseResult runPhase(Socket sender, Socket receiver,
                                        MemorySegment payload, int payloadSize,
                                        int msgSize, int runId, long[] seq,
                                        int phase, int warmupCount,
                                        int durationSeconds, int latencyCap,
                                        int recvTimeoutMs) {
        final boolean active = durationSeconds > 0;
        final long deadlineNs = active
            ? System.nanoTime()
                + (long) Math.max(durationSeconds, 1) * 1_000_000_000L
            : 0L;
        final long drainDeadlineNs =
            (long) Math.max(recvTimeoutMs, 1) * 1_000_000L;
        final LongAdder received = new LongAdder();
        final AtomicBoolean senderDone = new AtomicBoolean(false);
        final AtomicReference<RuntimeException> recvError =
            new AtomicReference<>();
        final PerfCommon.LatencyReservoir latency =
            new PerfCommon.LatencyReservoir(Math.max(latencyCap, 1));

        Thread receiverThread = new Thread(() -> {
            long lastRecvNs = System.nanoTime();
            PerfSingleMetricHeader.Header header =
                new PerfSingleMetricHeader.Header();

            try (Arena recvArena = Arena.ofConfined()) {
                MemorySegment recvBuffer = recvArena.allocate(payloadSize, 8);
                while (true) {
                    boolean done = senderDone.get();
                    int recvRc = receiveRouterPayload(receiver, recvBuffer,
                        done ? ReceiveFlag.DONTWAIT : ReceiveFlag.NONE);
                    if (recvRc > 0) {
                        lastRecvNs = System.nanoTime();
                        accountMessage(recvBuffer, recvRc, payloadSize, active,
                            runId, phase, header, received, latency);

                        while (true) {
                            recvRc = receiveRouterPayload(receiver, recvBuffer,
                                ReceiveFlag.DONTWAIT);
                            if (recvRc > 0) {
                                lastRecvNs = System.nanoTime();
                                accountMessage(recvBuffer, recvRc, payloadSize,
                                    active, runId, phase, header, received,
                                    latency);
                                continue;
                            }
                            if (recvRc == 0) {
                                break;
                            }
                            throw new IllegalStateException("router_recv_failed");
                        }
                        continue;
                    }
                    if (recvRc == 0) {
                        if (done
                            && System.nanoTime() - lastRecvNs >= drainDeadlineNs) {
                            break;
                        }
                        Thread.yield();
                        continue;
                    }
                    throw new IllegalStateException("router_recv_failed");
                }
            } catch (RuntimeException ex) {
                recvError.set(ex);
            }
        }, "zlink-java-perf-dealer-router-recv");
        receiverThread.setDaemon(true);
        receiverThread.start();

        boolean ok = true;
        if (active) {
            while (System.nanoTime() < deadlineNs) {
                if (!PerfSingleMetricHeader.stampPayload(payload, payloadSize,
                    runId, phase, msgSize, seq[0]++, PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                sendBlocking(sender, payload, payloadSize, SendFlag.NONE);
            }
        } else {
            for (int i = 0; i < warmupCount; i++) {
                if (!PerfSingleMetricHeader.stampPayload(payload, payloadSize,
                    runId, phase, msgSize, seq[0]++, PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                sendBlocking(sender, payload, payloadSize, SendFlag.NONE);
            }
        }

        senderDone.set(true);
        try {
            receiverThread.join();
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return new PhaseResult(false, received.sum(), latency);
        }

        return new PhaseResult(ok && recvError.get() == null, received.sum(),
            latency);
    }

    private static void accountMessage(MemorySegment recvBuffer, int bytesRead,
                                       int payloadSize, boolean active,
                                       int runId, int phase,
                                       PerfSingleMetricHeader.Header header,
                                       LongAdder received,
                                       PerfCommon.LatencyReservoir latency) {
        if (bytesRead != payloadSize) {
            return;
        }
        if (!PerfSingleMetricHeader.decodePayloadHeader(recvBuffer, payloadSize,
            header)) {
            return;
        }
        if (header.runId != runId || header.phase != phase) {
            return;
        }
        received.increment();
        if (!active) {
            return;
        }
        long nowUs = PerfSingleMetricHeader.nowUs();
        latency.add(Math.max(0L, nowUs - header.sentTsUs));
    }

    private static int receiveRouterPayload(Socket socket,
                                            MemorySegment payloadBuffer,
                                            ReceiveFlag flags) {
        try {
            if (!socket.recvFrameHasMore(flags)) {
                return -1;
            }
            return receive(socket, payloadBuffer, ReceiveFlag.NONE);
        } catch (ZlinkException ex) {
            if (isInterrupted(ex.errno())) {
                return 0;
            }
            if (isWouldBlock(ex.errno())) {
                return 0;
            }
            throw ex;
        }
    }

    private static int receive(Socket socket, MemorySegment buffer,
                               ReceiveFlag flags) {
        while (true) {
            try {
                return socket.recv(buffer, flags);
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

    private static void sendBlocking(Socket socket, MemorySegment payload,
                                     long payloadSize, SendFlag flags) {
        SendFlag op = flags == null ? SendFlag.NONE : flags;
        int written = socket.send(payload, 0, payloadSize, op);
        if (written != payloadSize) {
            throw new IllegalStateException("send_failed");
        }
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private record PhaseResult(boolean ok, long received,
                               PerfCommon.LatencyReservoir latency) {
    }
}
