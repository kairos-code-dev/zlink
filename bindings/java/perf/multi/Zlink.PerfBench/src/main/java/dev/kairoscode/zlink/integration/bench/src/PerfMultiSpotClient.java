/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_SPOT client benchmark.
 * SpotNode(connect) + Spot(subscribe) receives one-way stamped payloads.
 */
public final class PerfMultiSpotClient {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String TOPIC = "bench";
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfMultiSpotClient() {
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

            List<SpotNode> nodes = new ArrayList<>(clients);
            List<Spot> spots = new ArrayList<>(clients);
            List<Spot> activeSpots = new ArrayList<>(clients);

            try {
                for (int i = 0; i < clients; i++) {
                    SpotNode node = new SpotNode(context);
                    applySpotNodeOptions(node);
                    PerfMultiTls.configureSpotSubscriberTlsIfNeeded(node, transport);
                    node.connectPeerPub(endpoint);

                    Spot spot = new Spot(node);
                    spot.subscribe(TOPIC);

                    nodes.add(node);
                    spots.add(spot);
                }
                activeSpots.addAll(spots);
                if (activeSpots.isEmpty()) {
                    return 2;
                }
                waitSubPeers(nodes, connectTimeoutMs);

                byte[] recv = new byte[payloadSize];
                PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();

                int index = 0;
                // --- Warmup ---
                long warmupDeadline = System.nanoTime()
                    + (long) Math.max(0, warmupSeconds)
                    * NANOSECONDS_PER_SECOND;
                while (System.nanoTime() < warmupDeadline) {
                    Spot spot = activeSpots.get(index);
                    receiveSpotBlocking(spot, recv);
                    while (receiveSpotNonBlocking(spot, recv) > 0) {
                    }
                    index = (index + 1) % activeSpots.size();
                }

                // --- Settle ---
                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(
                        PerfMultiCommon.resolveLatencySampleCap());

                long count = 0;
                long benchDeadline = System.nanoTime()
                    + (long) Math.max(1, durationSeconds)
                    * NANOSECONDS_PER_SECOND;
                long benchBegin = System.nanoTime();

                // --- Active measurement ---
                while (System.nanoTime() < benchDeadline) {
                    Spot spot = activeSpots.get(index);
                    int n = receiveSpotBlocking(spot, recv);
                    if (n > 0) {
                        count += collectActiveSample(recv, n, header, msgSize,
                            reservoir);
                        while (true) {
                            int drained = receiveSpotNonBlocking(spot, recv);
                            if (drained <= 0) {
                                break;
                            }
                            count += collectActiveSample(recv, drained, header,
                                msgSize, reservoir);
                        }
                    }
                    index = (index + 1) % activeSpots.size();
                }

                double elapsedSec = Math.max(1e-9,
                    (System.nanoTime() - benchBegin) / 1_000_000_000.0);
                double throughput = count / elapsedSec;
                PerfCommon.Stats stats = reservoir.snapshot();
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            } finally {
                closeAll(spots);
                closeAll(nodes);
            }
        } catch (RuntimeException ignored) {
            return 2;
        }
    }

    private static long collectActiveSample(byte[] recv, int recvBytes,
                                            PerfMultiMetricHeader.Header header,
                                            int msgSize,
                                            PerfCommon.LatencyReservoir reservoir) {
        if (recvBytes < HEADER_BYTES
            || !PerfMultiMetricHeader.decodePayloadHeader(recv, header)
            || header.phase != PerfMultiMetricHeader.PHASE_ACTIVE
            || header.msgSize != msgSize) {
            return 0L;
        }
        long nowUs = PerfMultiMetricHeader.nowUs();
        reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
        return 1L;
    }

    private static int receiveSpotBlocking(Spot spot, byte[] payloadBuffer) {
        while (true) {
            try (Spot.SpotMessage message = spot.recv(ReceiveFlag.NONE)) {
                return copySpotPayload(message, payloadBuffer);
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

    private static int receiveSpotNonBlocking(Spot spot, byte[] payloadBuffer) {
        while (true) {
            try (Spot.SpotMessage message = spot.recv(ReceiveFlag.DONTWAIT)) {
                return copySpotPayload(message, payloadBuffer);
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

    private static int copySpotPayload(Spot.SpotMessage message,
                                       byte[] payloadBuffer) {
        Message[] parts = message.parts();
        if (parts.length == 0) {
            return 0;
        }
        Message payload = parts[parts.length - 1];
        int size = payload.size();
        if (size > payloadBuffer.length) {
            return 0;
        }
        if (size > 0) {
            payload.copyTo(payloadBuffer);
        }
        return size;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
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

    private static void applySpotNodeOptions(SpotNode node) {
        int rcvHwm = PerfMultiCommon.resolveRcvHwm(PATTERN);
        int rcvTimeoutMs = PerfMultiCommon.resolveRcvTimeoutMs();
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVHWM, rcvHwm);
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVTIMEO,
            rcvTimeoutMs);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.LINGER, 0);
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.LINGER, 0);
        node.setOption(SpotNodeSocketRole.DEALER, SocketOptions.LINGER, 0);
    }

    private static void waitSubPeers(List<SpotNode> nodes, int timeoutMs) {
        if (nodes.isEmpty()) {
            return;
        }
        long deadline = System.nanoTime()
            + (long) Math.max(500, timeoutMs * 3) * 1_000_000L;
        boolean[] ready = new boolean[nodes.size()];
        int readyCount = 0;

        while (System.nanoTime() < deadline && readyCount < nodes.size()) {
            for (int i = 0; i < nodes.size(); i++) {
                if (ready[i]) {
                    continue;
                }
                int peers;
                try {
                    peers = nodes.get(i).subPeers().size();
                } catch (RuntimeException ex) {
                    return;
                }
                if (peers > 0) {
                    ready[i] = true;
                    readyCount++;
                }
            }
            if (readyCount < nodes.size()) {
                PerfCommon.sleepMillis(5);
            }
        }
    }
}
