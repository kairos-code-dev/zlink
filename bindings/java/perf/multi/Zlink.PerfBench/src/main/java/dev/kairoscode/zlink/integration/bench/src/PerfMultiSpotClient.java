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
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_SPOT client benchmark.
 * Spot uses service-instance poller registration with facade recv.
 */
public final class PerfMultiSpotClient {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String TOPIC = "bench";
    private static final int HEADER_BYTES = 32;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;
    private static final long ACTIVE_SEARCH_EXTENSION_NS = 2L
        * NANOSECONDS_PER_SECOND;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class ClientConfig {
        final int clients;
        final int warmupSeconds;
        final int durationSeconds;
        final int settleMs;
        final int connectTimeoutMs;
        final int pollTimeoutMs;

        ClientConfig(int clients, int warmupSeconds, int durationSeconds,
                     int settleMs, int connectTimeoutMs,
                     int pollTimeoutMs) {
            this.clients = clients;
            this.warmupSeconds = warmupSeconds;
            this.durationSeconds = durationSeconds;
            this.settleMs = settleMs;
            this.connectTimeoutMs = connectTimeoutMs;
            this.pollTimeoutMs = pollTimeoutMs;
        }
    }

    private static final class ClientSlot {
        final SpotNode node;
        final Spot subscriber;
        final Spot.RecvContext recvContext;

        ClientSlot(SpotNode node, Spot subscriber,
                   Spot.RecvContext recvContext) {
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
            Math.max(5000, PerfMultiCommon.resolveConnectReadyTimeoutMs() * 3),
            Math.max(1, PerfMultiCommon.resolveClientPollTimeoutMs()));

        try (Context context = new Context()) {
            PerfCommon.applyClientContextOptions(context);

            List<ClientSlot> slots = new ArrayList<>(config.clients);
            try {
                for (int i = 0; i < config.clients; i++) {
                    SpotNode node = new SpotNode(context);
                    applySpotNodeOptions(node);
                    PerfMultiTls.configureSpotPublisherTlsIfNeeded(node,
                        transport);
                    PerfMultiTls.configureSpotSubscriberTlsIfNeeded(node,
                        transport);
                    node.bind(PerfCommon.endpointFor(transport,
                        "multi-spot-client-" + i));
                    node.connectPeerPub(endpoint);

                    Spot subscriber = new Spot(node);
                    subscriber.subscribe(TOPIC);
                    slots.add(new ClientSlot(node, subscriber,
                        subscriber.createRecvContext()));
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
                        poller.addSpotSub(slots.get(i).subscriber,
                            PollEventType.POLLIN.getValue(), Integer.valueOf(i));
                    }

                    PerfMultiMetricHeader.Header header =
                        new PerfMultiMetricHeader.Header();

                    // --- Warmup ---
                    runDrainPhase(poller, slots, Math.max(0,
                        config.warmupSeconds) * NANOSECONDS_PER_SECOND,
                        config.pollTimeoutMs);

                    // --- Settle ---
                    runDrainPhase(poller, slots, Math.max(0,
                        config.settleMs) * NANOSECONDS_PER_MILLISECOND,
                        config.pollTimeoutMs);

                    PerfCommon.LatencyReservoir reservoir =
                        new PerfCommon.LatencyReservoir(
                            PerfMultiCommon.resolveLatencySampleCap());

                    // --- Active measurement ---
                    ActiveResult active = runActive(poller, slots, header,
                        msgSize, reservoir, config.durationSeconds,
                        config.pollTimeoutMs);
                    if (active.count <= 0) {
                        System.err.println("ERROR,MULTI_SPOT,client,no_active_frames");
                        return 2;
                    }

                    double throughput = active.count
                        / Math.max(1.0, active.elapsedSec);
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

    private static final class ActiveResult {
        final long count;
        final double elapsedSec;

        ActiveResult(long count, double elapsedSec) {
            this.count = count;
            this.elapsedSec = elapsedSec;
        }
    }

    private static void runDrainPhase(Poller poller,
                                      List<ClientSlot> slots,
                                      long durationNs,
                                      int pollTimeoutMs) {
        if (durationNs <= 0L) {
            return;
        }
        long deadline = System.nanoTime() + durationNs;
        while (System.nanoTime() < deadline) {
            drainEvents(poller, slots, null, -1, null, -1, pollTimeoutMs);
        }
    }

    private static ActiveResult runActive(Poller poller,
                                          List<ClientSlot> slots,
                                          PerfMultiMetricHeader.Header header,
                                          int msgSize,
                                          PerfCommon.LatencyReservoir reservoir,
                                          int durationSeconds,
                                          int pollTimeoutMs) {
        long durationNs = (long) Math.max(1, durationSeconds)
            * NANOSECONDS_PER_SECOND;
        long deadline = System.nanoTime() + durationNs;
        long activeSearchDeadline = deadline + ACTIVE_SEARCH_EXTENSION_NS;
        long activeBeginNs = 0L;
        long count = 0L;
        int activeRunId = -1;

        while (System.nanoTime() < (activeBeginNs == 0L ? activeSearchDeadline
            : deadline)) {
            ActiveDrainResult drained = drainEvents(poller, slots, header,
                msgSize, reservoir, activeRunId, pollTimeoutMs);
            if (drained.count <= 0 || !drained.startedActive) {
                continue;
            }
            count += drained.count;
            if (activeRunId < 0) {
                activeRunId = drained.runId;
            }
            if (activeBeginNs == 0L) {
                activeBeginNs = System.nanoTime();
                deadline = activeBeginNs + durationNs;
            }
        }

        double elapsedSec = activeBeginNs == 0L ? 0.0
            : (System.nanoTime() - activeBeginNs) / 1_000_000_000.0;
        return new ActiveResult(count, elapsedSec);
    }

    private static final class ActiveDrainResult {
        long count;
        boolean startedActive;
        int runId = -1;
    }

    private static ActiveDrainResult drainEvents(Poller poller,
                                    List<ClientSlot> slots,
                                    PerfMultiMetricHeader.Header header,
                                    int msgSize,
                                    PerfCommon.LatencyReservoir reservoir,
                                    int activeRunId,
                                    int pollTimeoutMs) {
        ActiveDrainResult result = new ActiveDrainResult();
        int eventCount = poller.pollCount(pollTimeoutMs);
        for (int i = 0; i < eventCount; i++) {
            Object tag = poller.readyTag(i);
            if (!(tag instanceof Integer)) {
                continue;
            }
            int slotIndex = (Integer) tag;
            if (slotIndex < 0 || slotIndex >= slots.size()) {
                continue;
            }
            ClientSlot slot = slots.get(slotIndex);
            while (true) {
                Spot.SpotRawBorrowed raw = receiveSpotPayloadNonBlocking(slot);
                if (raw == null) {
                    break;
                }
                if (header == null || reservoir == null) {
                    continue;
                }
                long sampleCount = collectActiveSample(raw, header, msgSize,
                    reservoir, activeRunId);
                if (sampleCount <= 0L) {
                    continue;
                }
                if (!result.startedActive) {
                    result.startedActive = true;
                    result.runId = header.runId;
                }
                result.count += sampleCount;
            }
        }
        return result;
    }

    private static long collectActiveSample(Spot.SpotRawBorrowed raw,
                                            PerfMultiMetricHeader.Header header,
                                            int msgSize,
                                            PerfCommon.LatencyReservoir reservoir,
                                            int activeRunId) {
        if (!matchesTopic(raw.topicIdBuffer(), raw.topicIdLength())) {
            return 0L;
        }
        Message[] parts = raw.parts();
        if (parts.length != 1) {
            return 0L;
        }
        Message payload = parts[0];
        int payloadSize = payload.size();
        if (payloadSize < HEADER_BYTES
            || !PerfMultiMetricHeader.decodePayloadHeader(payload.dataSegment(),
            payloadSize, header)
            || header.phase != PerfMultiMetricHeader.PHASE_ACTIVE
            || header.msgSize != msgSize
            || (activeRunId >= 0 && header.runId != activeRunId)) {
            return 0L;
        }
        long nowUs = PerfMultiMetricHeader.nowUs();
        reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
        return 1L;
    }

    private static Spot.SpotRawBorrowed receiveSpotPayloadNonBlocking(
      ClientSlot slot) {
        while (true) {
            try {
                return slot.subscriber.recvRawBorrowed(ReceiveFlag.DONTWAIT,
                    slot.recvContext);
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno())) {
                    continue;
                }
                if (isWouldBlock(ex.errno())) {
                    return null;
                }
                throw ex;
            }
        }
    }

    private static boolean matchesTopic(MemorySegment topicBuffer, int topicLen) {
        if (topicBuffer == null
            || topicBuffer.address() == 0
            || topicLen != TOPIC.length()) {
            return false;
        }
        for (int i = 0; i < TOPIC.length(); i++) {
            if (topicBuffer.get(ValueLayout.JAVA_BYTE, i)
                != (byte) TOPIC.charAt(i)) {
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
        while (System.nanoTime() < deadline) {
            for (int i = 0; i < slots.size(); i++) {
                try {
                    if (slots.get(i).node.subPeers().size() > 0) {
                        return true;
                    }
                } catch (RuntimeException ex) {
                    return false;
                }
            }
            PerfCommon.sleepMillis(5);
        }
        return false;
    }
}
