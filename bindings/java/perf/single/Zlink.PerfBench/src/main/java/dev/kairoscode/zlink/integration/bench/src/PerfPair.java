/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
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
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.LongAdder;

/**
 * PAIR one-way benchmark.
 * sender=PAIR(connect), receiver=PAIR(bind), header-stamped payload.
 */
public final class PerfPair {
    private static final String PATTERN = "PAIR";

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int CONNECT_MONITOR_EVENTS =
        MonitorEventType.CONNECTION_READY.getValue()
            | MonitorEventType.CONNECTED.getValue()
            | MonitorEventType.ACCEPTED.getValue();

    private PerfPair() {
    }

    public static int run(String transport, int msgSize) {
        try (Context context = new Context();
             Socket receiver = new Socket(context, SocketType.PAIR);
             Socket sender = new Socket(context, SocketType.PAIR);
             MonitorSocket senderMonitor = sender.monitorOpen(CONNECT_MONITOR_EVENTS)) {
            PerfCommon.applySingleContextOptions(context);
            PerfCommon.applySingleSocketOptions(receiver);
            PerfCommon.applySingleSocketOptions(sender);

            PerfTls.configureTlsServerIfNeeded(receiver, transport);
            PerfTls.configureTlsClientIfNeeded(sender, transport);

            String endpoint = PerfCommon.endpointFor(transport, "pair");
            receiver.bind(endpoint);
            sender.connect(endpoint);
            if (!PerfCommon.waitConnectReady(transport, senderMonitor, 5000, true)) {
                return 2;
            }

            int payloadSize = Math.max(msgSize, Long.BYTES);
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment payload = arena.allocate(payloadSize, 8);
                payload.fill((byte) 'a');

                int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
                PhaseResult warmup = runPhase(sender, receiver, payload,
                    payloadSize, msgSize, warmupCount, 0, 0);
                if (!warmup.ok || warmup.received < warmupCount) {
                    return 2;
                }

                Thread.sleep(PerfCommon.resolveSettleMs());

                int durationSeconds = PerfCommon.resolveDurationSeconds();
                int latencyCap = PerfCommon.resolveLatencySampleCap();
                PhaseResult active = runPhase(sender, receiver, payload,
                    payloadSize, msgSize, 0, durationSeconds, latencyCap);
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
                                        int msgSize, int warmupCount,
                                        int durationSeconds, int latencyCap) {
        final boolean active = durationSeconds > 0;
        final long deadlineNs = active
            ? System.nanoTime()
                + (long) Math.max(durationSeconds, 1) * 1_000_000_000L
            : 0L;
        final long drainDeadlineNs =
            (long) Math.max(PerfCommon.resolveRecvTimeoutMs(), 1) * 1_000_000L;
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

            try {
                while (true) {
                    boolean done = senderDone.get();
                    try (Message msg = receiveMessage(receiver,
                        done ? ReceiveFlag.DONTWAIT : ReceiveFlag.NONE)) {
                        lastRecvNs = System.nanoTime();
                        accountMessage(msg, payloadSize, active, header, received,
                            latency);

                        while (true) {
                            try (Message burst = receiveMessage(receiver,
                                ReceiveFlag.DONTWAIT)) {
                                lastRecvNs = System.nanoTime();
                                accountMessage(burst, payloadSize, active, header,
                                    received, latency);
                            }
                        }
                    } catch (ZlinkException ex) {
                        if (isInterrupted(ex.errno())) {
                            continue;
                        }
                        if (isWouldBlock(ex.errno())) {
                            if (done
                                && System.nanoTime() - lastRecvNs >= drainDeadlineNs) {
                                break;
                            }
                            Thread.yield();
                            continue;
                        }
                        throw ex;
                    }
                }
            } catch (RuntimeException ex) {
                recvError.set(ex);
            }
        }, "zlink-java-perf-pair-recv");
        receiverThread.setDaemon(true);
        receiverThread.start();

        boolean ok = true;
        if (active) {
            while (System.nanoTime() < deadlineNs) {
                if (!PerfSingleMetricHeader.stampPayload(payload, payloadSize, 0,
                    PerfSingleMetricHeader.PHASE_ACTIVE, msgSize, 0,
                    PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                sendBlocking(sender, payload, payloadSize, SendFlag.NONE);
            }
        } else {
            for (int i = 0; i < warmupCount; i++) {
                if (!PerfSingleMetricHeader.stampPayload(payload, payloadSize, 0,
                    PerfSingleMetricHeader.PHASE_WARMUP, msgSize, i + 1L,
                    PerfSingleMetricHeader.nowUs())) {
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

    private static void accountMessage(Message msg, int payloadSize, boolean active,
                                       PerfSingleMetricHeader.Header header,
                                       LongAdder received,
                                       PerfCommon.LatencyReservoir latency) {
        if (msg.more() || msg.size() != payloadSize) {
            return;
        }
        if (!PerfSingleMetricHeader.decodePayloadHeader(msg.dataSegment(),
            payloadSize, header)) {
            return;
        }
        received.increment();
        if (!active) {
            return;
        }
        long nowUs = PerfSingleMetricHeader.nowUs();
        latency.add(Math.max(0L, nowUs - header.sentTsUs));
    }

    private static Message receiveMessage(Socket socket, ReceiveFlag flag) {
        Message msg = new Message();
        try {
            msg.recv(socket, flag);
            return msg;
        } catch (RuntimeException ex) {
            msg.close();
            throw ex;
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
