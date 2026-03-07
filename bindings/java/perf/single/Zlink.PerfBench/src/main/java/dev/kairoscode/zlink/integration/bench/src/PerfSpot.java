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

/**
 * SPOT benchmark using high-level Spot API.
 * publisher=Spot(pub), subscriber=Spot(sub + topic).
 */
public final class PerfSpot {
    private static final String PATTERN = "SPOT";
    private static final String TOPIC = "bench";

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

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

            String endpoint = PerfCommon.endpointFor(tr, "spot");
            PerfTls.configureSpotPublisherTlsIfNeeded(pubNode, tr);
            PerfTls.configureSpotSubscriberTlsIfNeeded(subNode, tr);
            subNode.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVTIMEO, 5000);
            pubNode.bind(endpoint);
            subNode.connectPeerPub(endpoint);

            try (Spot publisher = new Spot(pubNode);
                 Spot subscriber = new Spot(subNode)) {
                subscriber.subscribe(TOPIC);
                if (!PerfCommon.waitUntil(() -> !subNode.subPeers().isEmpty(),
                    5000,
                    5)) {
                    return 2;
                }

                int payloadSize = Math.max(msgSize,
                    PerfSingleMetricHeader.HEADER_SIZE);
                byte[] payload = new byte[payloadSize];
                byte[] recv = new byte[payloadSize];

                int runId = PerfCommon.randomRunId();
                long seq = 1;
                try (Message payloadMessage = new Message(payloadSize);
                     Spot.RecvContext recvContext = subscriber.createRecvContext()) {
                    MemorySegment payloadSegment = MemorySegment.ofArray(payload);

                    int warmupCount = PerfCommon.resolveWarmupCount(PATTERN, msgSize);
                    for (int i = 0; i < warmupCount; i++) {
                        PerfSingleMetricHeader.stampPayload(payload, runId,
                            PerfSingleMetricHeader.PHASE_WARMUP, msgSize, seq++,
                            PerfSingleMetricHeader.nowUs());
                        MemorySegment.copy(payloadSegment, 0,
                            payloadMessage.dataSegment(), 0, payloadSize);
                        publisher.publish(TOPIC, payloadMessage, SendFlag.NONE);
                        receiveSpotBlocking(subscriber, recvContext, recv);
                        while (receiveSpotNonBlocking(subscriber, recvContext,
                            recv) > 0) {
                        }
                    }

                    Thread.sleep(PerfCommon.resolveSettleMs());

                    PerfSingleMetricHeader.Header header =
                        new PerfSingleMetricHeader.Header();
                    PerfCommon.LatencyReservoir latency =
                        new PerfCommon.LatencyReservoir(
                            PerfCommon.resolveLatencySampleCap());

                    long received = 0;
                    long endNs = System.nanoTime()
                        + (long) PerfCommon.resolveDurationSeconds() * 1_000_000_000L;
                    long beginNs = System.nanoTime();

                    while (System.nanoTime() < endNs) {
                        PerfSingleMetricHeader.stampPayload(payload, runId,
                            PerfSingleMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                            PerfSingleMetricHeader.nowUs());
                        MemorySegment.copy(payloadSegment, 0,
                            payloadMessage.dataSegment(), 0, payloadSize);
                        publisher.publish(TOPIC, payloadMessage, SendFlag.NONE);

                        int n = receiveSpotBlocking(subscriber, recvContext, recv);
                        if (n > 0) {
                            received += collectActiveSample(recv, n, runId, header,
                                latency);
                            while (true) {
                                int drained = receiveSpotNonBlocking(subscriber,
                                    recvContext, recv);
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

    private static int receiveSpotBlocking(Spot spot,
                                           Spot.RecvContext recvContext,
                                           byte[] payloadBuffer) {
        while (true) {
            try {
                return copySpotPayload(spot.recvRawBorrowed(ReceiveFlag.NONE,
                    recvContext), payloadBuffer);
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

    private static int receiveSpotNonBlocking(Spot spot,
                                              Spot.RecvContext recvContext,
                                              byte[] payloadBuffer) {
        while (true) {
            try {
                return copySpotPayload(spot.recvRawBorrowed(ReceiveFlag.DONTWAIT,
                    recvContext), payloadBuffer);
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

    private static int copySpotPayload(Spot.SpotRawBorrowed message,
                                       byte[] payloadBuffer) {
        Message[] parts = message.parts();
        if (parts.length == 0) {
            return 0;
        }
        Message payload = parts[parts.length - 1];
        int size = payload.size();
        int copyLen = Math.min(size, payloadBuffer.length);
        if (copyLen > 0) {
            payload.copyTo(payloadBuffer, 0);
        }
        return copyLen;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }
}
