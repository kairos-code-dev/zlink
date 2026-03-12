/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfSingleMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import java.lang.foreign.MemorySegment;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import java.util.concurrent.atomic.LongAdder;

public final class PerfSpot {
    private static final String PATTERN = "SPOT";
    private static final String TOPIC = "bench";

    private PerfSpot() {
    }

    public static int run(String transport, int msgSize) {
        String tr = transport == null ? "" : transport.toLowerCase();
        if ("inproc".equals(tr) || "ipc".equals(tr)) {
            PerfCommon.printUnsupported(PATTERN, tr, msgSize,
                "spot does not support inproc/ipc");
            return 0;
        }

        try (Context context = new Context();
             SpotNode pubNode = new SpotNode(context);
             SpotNode subNode = new SpotNode(context)) {
            PerfCommon.applySingleContextOptions(context);
            int baseHwm = PerfCommon.parseEnvNonNegative("PERF_SINGLE_HWM", 1000);
            int sndHwm = PerfCommon.parseEnvNonNegative("PERF_SINGLE_SNDHWM",
                baseHwm);
            int rcvHwm = PerfCommon.parseEnvNonNegative("PERF_SINGLE_RCVHWM",
                baseHwm);
            int sndTimeo = PerfCommon.parseEnvNonNegative("PERF_SINGLE_SNDTIMEO_MS",
                200);
            int recvTimeo = PerfCommon.resolveRecvTimeoutMs();
            int readyTimeoutMs = PerfCommon.resolveSpotDiscoveryTimeoutMs();

            pubNode.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDHWM,
                sndHwm);
            pubNode.setOption(SpotNodeSocketRole.PUB, SocketOptions.SNDTIMEO,
                sndTimeo);
            pubNode.setOption(SpotNodeSocketRole.PUB, SocketOptions.XPUB_NODROP,
                1);

            subNode.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVHWM,
                rcvHwm);
            subNode.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVTIMEO,
                recvTimeo);

            PerfTls.configureSpotPublisherTlsIfNeeded(pubNode, tr);
            PerfTls.configureSpotSubscriberTlsIfNeeded(subNode, tr);

            String endpoint = PerfCommon.endpointFor(tr, "spot");
            pubNode.bind(endpoint);
            subNode.connectPeerPub(endpoint);

            try (Spot publisher = new Spot(pubNode);
                 Spot subscriber = new Spot(subNode)) {
                subscriber.subscribe(TOPIC);
                if (!PerfCommon.waitUntil(() -> !subNode.subPeers().isEmpty(),
                    readyTimeoutMs, 1)) {
                    return 2;
                }

                int payloadSize = Math.max(msgSize,
                    PerfSingleMetricHeader.HEADER_SIZE);
                byte[] payload = new byte[payloadSize];
                MemorySegment payloadArray = MemorySegment.ofArray(payload);
                int runId = PerfCommon.randomRunId();
                long[] seq = new long[] {1L};

                if (!waitForSubscriptionReady(publisher, subscriber, payload,
                    PerfCommon.resolveSpotReadyTimeoutMs()))
                    return 2;

                int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
                if (msgSize >= 65536 && warmupCount > 20)
                    warmupCount = 20;

                try (Message publishMessage = new Message(payloadSize)) {
                    PhaseResult warmup = runPhase(publisher, subscriber,
                        publishMessage, payload, payloadArray, payloadSize, msgSize,
                        runId, seq, PerfSingleMetricHeader.PHASE_WARMUP,
                        warmupCount, 0, 0, PerfCommon.resolveRecvTimeoutMs());
                    if (!warmup.ok || warmup.received < warmupCount) {
                        return 2;
                    }

                    Thread.sleep(PerfCommon.resolveSettleMs());

                    PhaseResult active = runPhase(publisher, subscriber,
                        publishMessage, payload, payloadArray, payloadSize, msgSize,
                        runId, seq, PerfSingleMetricHeader.PHASE_ACTIVE, 0,
                        PerfCommon.resolveDurationSeconds(),
                        PerfCommon.resolveLatencySampleCap(),
                        PerfCommon.resolveRecvTimeoutMs());
                    if (!active.ok || active.received <= 0) {
                        return 2;
                    }

                    double throughput =
                        active.received / (double) Math.max(
                            PerfCommon.resolveDurationSeconds(), 1);
                    PerfCommon.Stats stats = active.latency.snapshot();
                    PerfCommon.printResult(PATTERN, tr, msgSize, throughput,
                        stats.meanUs(), stats.p95Us(), stats.p99Us());
                    return 0;
                }
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            return 2;
        } catch (RuntimeException ex) {
            return 2;
        }
    }

    private static boolean waitForSubscriptionReady(Spot publisher,
                                                    Spot subscriber,
                                                    byte[] payload,
                                                    int timeoutMs) {
        long deadline = System.nanoTime()
            + (long) Math.max(timeoutMs, 1) * 1_000_000L;
        while (System.nanoTime() < deadline) {
            publisher.publish(TOPIC, Message.fromBytes(payload), SendFlag.NONE);
            try (Spot.RecvContext recvContext = subscriber.createRecvContext()) {
                Spot.SpotRawBorrowed message =
                    subscriber.recvRawBorrowed(ReceiveFlag.NONE, recvContext);
                if (message.parts().length > 0) {
                    return true;
                }
            } catch (ZlinkException ex) {
                if (ex.errno() != 4 && ex.errno() != 11 && ex.errno() != 10035) {
                    throw ex;
                }
            }
        }
        return false;
    }

    private static PhaseResult runPhase(Spot publisher, Spot subscriber,
                                        Message publishMessage,
                                        byte[] payload,
                                        MemorySegment payloadArray,
                                        int payloadSize,
                                        int msgSize,
                                        int runId,
                                        long[] seq,
                                        int phase,
                                        int warmupCount,
                                        int durationSeconds,
                                        int latencyCap,
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

            try (Spot.RecvContext recvContext = subscriber.createRecvContext()) {
                while (true) {
                    boolean done = senderDone.get();
                    try {
                        Spot.SpotRawBorrowed message = subscriber.recvRawBorrowed(
                            done ? ReceiveFlag.DONTWAIT : ReceiveFlag.NONE,
                            recvContext);
                        lastRecvNs = System.nanoTime();
                        accountMessage(message, payloadSize, active, runId, phase,
                            header, received, latency);

                        while (true) {
                            message = subscriber.recvRawBorrowed(
                                ReceiveFlag.DONTWAIT, recvContext);
                            lastRecvNs = System.nanoTime();
                            accountMessage(message, payloadSize, active, runId,
                                phase, header, received, latency);
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
        }, "zlink-java-perf-spot-recv");
        receiverThread.setDaemon(true);
        receiverThread.start();

        boolean ok = true;
        if (active) {
            while (System.nanoTime() < deadlineNs) {
                if (!PerfSingleMetricHeader.stampPayload(payload, runId, phase,
                    msgSize, seq[0]++, PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                MemorySegment.copy(payloadArray, 0, publishMessage.dataSegment(),
                    0, payloadSize);
                publisher.publish(TOPIC, publishMessage, SendFlag.NONE);
            }
        } else {
            for (int i = 0; i < warmupCount; i++) {
                if (!PerfSingleMetricHeader.stampPayload(payload, runId, phase,
                    msgSize, seq[0]++, PerfSingleMetricHeader.nowUs())) {
                    ok = false;
                    break;
                }
                MemorySegment.copy(payloadArray, 0, publishMessage.dataSegment(),
                    0, payloadSize);
                publisher.publish(TOPIC, publishMessage, SendFlag.NONE);
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

    private static void accountMessage(Spot.SpotRawBorrowed message,
                                       int payloadSize,
                                       boolean active,
                                       int runId,
                                       int phase,
                                       PerfSingleMetricHeader.Header header,
                                       LongAdder received,
                                       PerfCommon.LatencyReservoir latency) {
        Message[] parts = message.parts();
        if (parts.length == 0) {
            return;
        }
        Message payload = parts[parts.length - 1];
        if (payload.more() || payload.size() != payloadSize
            || !PerfSingleMetricHeader.decodePayloadHeader(payload.dataSegment(),
                payloadSize, header)
            || header.runId != runId
            || header.phase != phase) {
            return;
        }
        received.increment();
        if (!active) {
            return;
        }
        long nowUs = PerfSingleMetricHeader.nowUs();
        latency.add(Math.max(0L, nowUs - header.sentTsUs));
    }

    private static boolean isWouldBlock(int errno) {
        return errno == 11 || errno == 10035;
    }

    private static boolean isInterrupted(int errno) {
        return errno == 4;
    }

    private record PhaseResult(boolean ok, long received,
                               PerfCommon.LatencyReservoir latency) {
    }
}
