/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
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
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_SPOT client benchmark.
 * Spot service instance receives payload, poller registers SpotSub directly.
 */
public final class PerfMultiSpotClient {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String TOPIC = "bench";
    private static final byte[] TOPIC_BYTES = TOPIC.getBytes(
        StandardCharsets.UTF_8);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class ClientConfig {
        final int clients;
        final int warmupSeconds;
        final int durationSeconds;
        final int settleMs;
        final int connectTimeoutMs;
        final int payloadSize;
        final int pollTimeoutMs;

        ClientConfig(int clients, int warmupSeconds, int durationSeconds,
                     int settleMs, int connectTimeoutMs, int payloadSize,
                     int pollTimeoutMs) {
            this.clients = clients;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
            this.connectTimeoutMs = connectTimeoutMs;
            this.payloadSize = payloadSize;
            this.pollTimeoutMs = pollTimeoutMs;
        }
    }

    private static final class ClientSlot {
        final SpotNode node;
        final Spot subscriber;
        final Spot.RecvContext recvContext;

        ClientSlot(SpotNode node, Spot subscriber, Spot.RecvContext recvContext) {
            this.node = node;
            this.subscriber = subscriber;
            this.recvContext = recvContext;
        }
    }

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

        ClientConfig config = new ClientConfig(
            PerfMultiCommon.resolveClients(PATTERN),
            PerfMultiCommon.resolveWarmupSeconds(),
            PerfMultiCommon.resolveDurationSeconds(),
            PerfMultiCommon.resolveSettleMs(),
            PerfMultiCommon.resolveConnectReadyTimeoutMs(),
            Math.max(msgSize, MIN_PAYLOAD_BYTES),
            Math.max(1, PerfMultiCommon.resolveClientPollTimeoutMs()));

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<ClientSlot> slots = new ArrayList<>(config.clients);
            try {
                for (int i = 0; i < config.clients; i++) {
                    SpotNode node = new SpotNode(context);
                    applySpotNodeOptions(node);
                    PerfMultiTls.configureSpotSubscriberTlsIfNeeded(node,
                        transport);
                    node.connectPeerPub(endpoint);

                    Spot subscriber = new Spot(node);
                    subscriber.subscribe(TOPIC);
                    Spot.RecvContext recvContext = subscriber.createRecvContext();
                    slots.add(new ClientSlot(node, subscriber, recvContext));
                }

                if (slots.isEmpty()) {
                    System.err.println("ERROR,MULTI_SPOT,client,no_sockets");
                    return 2;
                }

                if (!waitSubPeers(slots, config.connectTimeoutMs)) {
                    System.err.println("ERROR,MULTI_SPOT,client,no_sub_peers");
                    return 2;
                }

                try (Poller poller = new Poller()) {
                    for (int i = 0; i < slots.size(); i++) {
                        poller.addSpotSub(slots.get(i).subscriber, i,
                            PollEventType.POLLIN);
                    }

                    byte[] recv = new byte[config.payloadSize];
                    PerfMultiMetricHeader.Header header =
                        new PerfMultiMetricHeader.Header();

                    // --- Warmup ---
                    runDrainPhase(poller, slots, recv, Math.max(0,
                        config.warmupSeconds) * NANOSECONDS_PER_SECOND,
                        config.pollTimeoutMs);

                    // --- Settle ---
                    runDrainPhase(poller, slots, recv, Math.max(0,
                        config.settleMs) * NANOSECONDS_PER_MILLISECOND,
                        config.pollTimeoutMs);

                    PerfCommon.LatencyReservoir reservoir =
                        new PerfCommon.LatencyReservoir(
                            PerfMultiCommon.resolveLatencySampleCap());

                    long count = 0;
                    long benchBegin = System.nanoTime();
                    long benchDeadline = benchBegin + (long) Math.max(1,
                        config.durationSeconds) * NANOSECONDS_PER_SECOND;

                    // --- Active measurement ---
                    while (System.nanoTime() < benchDeadline) {
                        count += drainActiveSamples(poller, slots, recv, header,
                            msgSize, reservoir, config.pollTimeoutMs);
                    }

                    double elapsedSec = Math.max(1e-9,
                        (System.nanoTime() - benchBegin) / 1_000_000_000.0);
                    double throughput = count / elapsedSec;
                    PerfCommon.Stats stats = reservoir.snapshot();
                    PerfCommon.printResult(PATTERN, transport, msgSize,
                        throughput, stats.meanUs(), stats.p95Us(),
                        stats.p99Us());
                    return 0;
                }
            } finally {
                closeSlots(slots);
            }
        } catch (RuntimeException ex) {
            System.err.println("ERROR,MULTI_SPOT,client,"
                + ex.getClass().getSimpleName() + ","
                + String.valueOf(ex.getMessage()));
            return 2;
        }
    }

    private static void runDrainPhase(Poller poller,
                                      List<ClientSlot> slots,
                                      byte[] payloadBuffer,
                                      long durationNs,
                                      int pollTimeoutMs) {
        if (durationNs <= 0L) {
            return;
        }
        long deadline = System.nanoTime() + durationNs;
        while (System.nanoTime() < deadline) {
            List<Poller.PollEvent> events = poller.poll(pollTimeoutMs);
            drainEvents(events, slots, payloadBuffer, null, -1, null);
        }
    }

    private static long drainActiveSamples(Poller poller,
                                           List<ClientSlot> slots,
                                           byte[] payloadBuffer,
                                           PerfMultiMetricHeader.Header header,
                                           int msgSize,
                                           PerfCommon.LatencyReservoir reservoir,
                                           int pollTimeoutMs) {
        List<Poller.PollEvent> events = poller.poll(pollTimeoutMs);
        return drainEvents(events, slots, payloadBuffer, header, msgSize,
            reservoir);
    }

    private static long drainEvents(List<Poller.PollEvent> events,
                                    List<ClientSlot> slots,
                                    byte[] payloadBuffer,
                                    PerfMultiMetricHeader.Header header,
                                    int msgSize,
                                    PerfCommon.LatencyReservoir reservoir) {
        long count = 0;
        int eventCount = events.size();
        for (int i = 0; i < eventCount; i++) {
            Object tag = events.get(i).tag();
            if (!(tag instanceof Integer)) {
                continue;
            }
            int slotIndex = (Integer) tag;
            if (slotIndex < 0 || slotIndex >= slots.size()) {
                continue;
            }
            ClientSlot slot = slots.get(slotIndex);
            while (true) {
                int n = receiveSpotPayloadNonBlocking(slot, payloadBuffer);
                if (n <= 0) {
                    break;
                }
                if (header == null || reservoir == null) {
                    continue;
                }
                count += collectActiveSample(payloadBuffer, n, header, msgSize,
                    reservoir);
            }
        }
        return count;
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

    private static int receiveSpotPayloadNonBlocking(ClientSlot slot,
                                                     byte[] payloadBuffer) {
        Spot.SpotRawBorrowed raw;
        while (true) {
            try {
                raw = slot.subscriber.recvRawBorrowed(ReceiveFlag.DONTWAIT,
                    slot.recvContext);
                break;
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

        if (!matchesTopic(raw.topicIdBuffer(), raw.topicIdLength())) {
            return 0;
        }
        Message[] parts = raw.parts();
        if (parts.length == 0) {
            return 0;
        }
        Message payload = parts[parts.length - 1];
        int payloadLen = payload.size();
        if (payloadLen > payloadBuffer.length) {
            throw new IllegalStateException("spot_payload_too_large");
        }
        payload.copyTo(payloadBuffer, 0);
        return payloadLen;
    }

    private static boolean matchesTopic(MemorySegment topicBuffer,
                                        int topicLen) {
        if (topicLen != TOPIC_BYTES.length
            || topicBuffer == null
            || topicBuffer.address() == 0) {
            return false;
        }
        for (int i = 0; i < TOPIC_BYTES.length; i++) {
            if (topicBuffer.get(ValueLayout.JAVA_BYTE, i) != TOPIC_BYTES[i]) {
                return false;
            }
        }
        return true;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static void closeSlots(List<ClientSlot> slots) {
        for (int i = slots.size() - 1; i >= 0; i--) {
            ClientSlot slot = slots.get(i);
            closeQuietly(slot.recvContext);
            closeQuietly(slot.subscriber);
            closeQuietly(slot.node);
        }
    }

    private static void closeQuietly(AutoCloseable resource) {
        if (resource == null) {
            return;
        }
        try {
            resource.close();
        } catch (Exception ignored) {
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

    private static boolean waitSubPeers(List<ClientSlot> slots, int timeoutMs) {
        if (slots.isEmpty()) {
            return false;
        }
        long deadline = System.nanoTime()
            + (long) Math.max(500, timeoutMs * 3) * 1_000_000L;
        boolean[] ready = new boolean[slots.size()];
        int readyCount = 0;

        while (System.nanoTime() < deadline && readyCount < slots.size()) {
            for (int i = 0; i < slots.size(); i++) {
                if (ready[i]) {
                    continue;
                }
                int peers;
                try {
                    peers = slots.get(i).node.subPeers().size();
                } catch (RuntimeException ex) {
                    return false;
                }
                if (peers > 0) {
                    ready[i] = true;
                    readyCount++;
                }
            }
            if (readyCount < slots.size()) {
                PerfCommon.sleepMillis(5);
            }
        }
        return readyCount == slots.size();
    }
}
