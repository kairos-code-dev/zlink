/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.spot.SpotNodeSocketRole;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_SPOT client benchmark.
 * Spot uses service-instance poller registration with facade recv.
 */
public final class PerfMultiSpotClient {
    private static final String PATTERN = "MULTI_SPOT";
    private static final String SERVICE_NAME = "perf-spot";
    private static final String TOPIC = "bench";
    private static final int RUN_ID = 1;
    private static final int HEADER_BYTES = 32;
    private static final int INITIAL_SETTLE_MS = 1000;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;
    private static final long ACTIVE_IDLE_TIMEOUT_NS = 1L
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

        ClientSlot(SpotNode node, Spot subscriber) {
            this.node = node;
            this.subscriber = subscriber;
        }
    }

    private PerfMultiSpotClient() {
    }

    public static int runClient(String transport, int msgSize, String endpointArg) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }
        if (endpointArg == null || endpointArg.isBlank()) {
            return 1;
        }
        ReadyEndpoint endpoint = parseReadyEndpoint(endpointArg);
        if (endpoint == null) {
            return 1;
        }

        ClientConfig config = new ClientConfig(
            PerfMultiCommon.resolveClients(PATTERN),
            PerfMultiCommon.resolveWarmupSeconds(),
            PerfMultiCommon.resolveDurationSeconds(),
            PerfMultiCommon.resolveSettleMs(),
            Math.max(5000, PerfMultiCommon.resolveConnectReadyTimeoutMs() * 3),
            PerfMultiCommon.resolveEffectiveClientPollTimeoutMs());

        try (Context context = new Context();
             Discovery discovery = new Discovery(context, ServiceType.SPOT)) {
            PerfCommon.applyClientContextOptions(context);
            discovery.connectRegistry(endpoint.registryRouter());
            discovery.subscribe(SERVICE_NAME);

            if (!PerfCommon.waitUntil(
                () -> discovery.serviceAvailable(SERVICE_NAME)
                    || discovery.receiverCount(SERVICE_NAME) > 0,
                config.connectTimeoutMs,
                5)) {
                System.err.println("ERROR,MULTI_SPOT,client,no_service");
                return 2;
            }

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
                    node.setDiscovery(discovery, SERVICE_NAME);

                    Spot subscriber = new Spot(node);
                    subscriber.subscribe(TOPIC);
                    node.setDiscovery(discovery, SERVICE_NAME);
                    slots.add(new ClientSlot(node, subscriber));
                }

                if (slots.isEmpty()) {
                    System.err.println("ERROR,MULTI_SPOT,client,no_sockets");
                    return 2;
                }

                if (!waitSubPeers(slots, config.connectTimeoutMs)) {
                    System.err.println("ERROR,MULTI_SPOT,client,no_sub_peers");
                    return 2;
                }
                PerfCommon.sleepMillis(Math.max(INITIAL_SETTLE_MS,
                    config.settleMs));

                try (Arena recvArena = Arena.ofConfined();
                     Poller poller = new Poller()) {
                    for (int i = 0; i < slots.size(); i++) {
                        poller.addSpotSub(slots.get(i).subscriber,
                            PollEventType.POLLIN.getValue(), Integer.valueOf(i));
                    }

                    PerfMultiMetricHeader.Header header =
                        new PerfMultiMetricHeader.Header();
                    MemorySegment recvBuffer =
                        recvArena.allocate(Math.max(HEADER_BYTES, msgSize));

                    int activeRunId = waitForMsgSizeStart(poller, slots, header,
                        recvBuffer, msgSize, config.pollTimeoutMs,
                        config.connectTimeoutMs);
                    if (activeRunId < 0) {
                        System.err.println("ERROR,MULTI_SPOT,client,no_msg_size_start");
                        return 2;
                    }

                    PerfCommon.LatencyReservoir reservoir =
                        new PerfCommon.LatencyReservoir(
                            PerfMultiCommon.resolveLatencySampleCap());

                    // --- Active measurement ---
                    ActiveResult active = runActive(poller, slots, header,
                        recvBuffer, msgSize, reservoir, activeRunId,
                        config.durationSeconds, config.pollTimeoutMs);
                    if (active.count <= 0) {
                        System.err.println("ERROR,MULTI_SPOT,client,no_active_frames");
                        return 2;
                    }

                    double throughput = active.count
                        / Math.max(1.0, config.durationSeconds);
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

        ActiveResult(long count) {
            this.count = count;
        }
    }

    private static ActiveResult runActive(Poller poller,
                                          List<ClientSlot> slots,
                                          PerfMultiMetricHeader.Header header,
                                          MemorySegment recvBuffer,
                                          int msgSize,
                                          PerfCommon.LatencyReservoir reservoir,
                                          int activeRunId,
                                          int durationSeconds,
                                          int pollTimeoutMs) {
        long durationNs = (long) Math.max(1, durationSeconds)
            * NANOSECONDS_PER_SECOND;
        long activeBeginNs = System.nanoTime();
        long activeDeadlineNs = activeBeginNs + durationNs;
        long count = 0L;

        while (System.nanoTime() < activeDeadlineNs) {
            ActiveDrainResult drained = drainEvents(poller, slots, header,
                recvBuffer, msgSize, reservoir, activeRunId, pollTimeoutMs,
                activeDeadlineNs);
            if (drained.count <= 0L) {
                continue;
            }
            count += drained.count;
        }

        return new ActiveResult(count);
    }

    private static final class ActiveDrainResult {
        long count;
        long drainedFrames;
        boolean startedActive;
        int runId = -1;
    }

    private static ActiveDrainResult drainEvents(Poller poller,
                                    List<ClientSlot> slots,
                                    PerfMultiMetricHeader.Header header,
                                    MemorySegment recvBuffer,
                                    int msgSize,
                                    PerfCommon.LatencyReservoir reservoir,
                                    int activeRunId,
                                    int pollTimeoutMs,
                                    long deadlineNs) {
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
                if (deadlineNs > 0L && System.nanoTime() >= deadlineNs) {
                    return result;
                }
                int payloadSize = receiveSpotPayloadNonBlocking(slot, recvBuffer);
                if (payloadSize <= 0) {
                    break;
                }
                result.drainedFrames++;
                if (header == null) {
                    continue;
                }
                long sampleCount = collectActiveSample(recvBuffer, payloadSize,
                    header, msgSize, reservoir, activeRunId);
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

    private static int waitForMsgSizeStart(Poller poller,
                                           List<ClientSlot> slots,
                                           PerfMultiMetricHeader.Header header,
                                           MemorySegment recvBuffer,
                                           int msgSize,
                                           int pollTimeoutMs,
                                           int timeoutMs) {
        long deadlineNs = System.nanoTime()
            + (long) Math.max(15000, timeoutMs * 2)
            * NANOSECONDS_PER_MILLISECOND;
        while (System.nanoTime() < deadlineNs) {
            ActiveDrainResult drained = drainEvents(poller, slots, header,
                recvBuffer, msgSize, null, -1, pollTimeoutMs, deadlineNs);
            if (drained.startedActive) {
                return drained.runId;
            }
        }
        return -1;
    }

    private static long collectActiveSample(MemorySegment payload,
                                            int payloadSize,
                                            PerfMultiMetricHeader.Header header,
                                            int msgSize,
                                            PerfCommon.LatencyReservoir reservoir,
                                            int activeRunId) {
        if (payloadSize < HEADER_BYTES
            || !PerfMultiMetricHeader.decodePayloadHeader(payload, payloadSize,
            header)
            || header.runId != RUN_ID
            || header.msgSize != msgSize
            || (activeRunId >= 0 && header.runId != activeRunId)) {
            return 0L;
        }
        long nowUs = PerfMultiMetricHeader.nowUs();
        if (reservoir != null) {
            reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
        }
        return 1L;
    }

    private static int receiveSpotPayloadNonBlocking(ClientSlot slot,
                                                     MemorySegment recvBuffer) {
        while (true) {
            try {
                return slot.subscriber.recvSinglePayload(recvBuffer,
                    ReceiveFlag.DONTWAIT);
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

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static void closeSlots(List<ClientSlot> slots) {
        for (int i = slots.size() - 1; i >= 0; i--) {
            ClientSlot slot = slots.get(i);
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
        int sndHwm = PerfMultiCommon.resolveSndHwm(PATTERN);
        int rcvHwm = PerfMultiCommon.resolveRcvHwm(PATTERN);
        int rcvTimeoutMs = PerfMultiCommon.resolveRcvTimeoutMs();
        int xpubNoDrop = resolveXpubNoDrop();
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVHWM, rcvHwm);
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.RCVTIMEO,
            rcvTimeoutMs);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.XPUB_NODROP,
            xpubNoDrop);
        node.setOption(SpotNodeSocketRole.PUB, SocketOptions.LINGER, 0);
        node.setOption(SpotNodeSocketRole.SUB, SocketOptions.LINGER, 0);
    }

    private static boolean waitSubPeers(List<ClientSlot> slots, int timeoutMs) {
        if (slots.isEmpty()) {
            return false;
        }
        long deadline = System.nanoTime()
            + (long) Math.max(500, timeoutMs * 3) * 1_000_000L;
        while (System.nanoTime() < deadline) {
            int readyCount = 0;
            for (int i = 0; i < slots.size(); i++) {
                try {
                    if (slots.get(i).node.subPeers().size() > 0) {
                        readyCount++;
                    }
                } catch (RuntimeException ex) {
                    return false;
                }
            }
            if (readyCount == slots.size()) {
                return true;
            }
            PerfCommon.sleepMillis(5);
        }
        return false;
    }

    private static int resolveXpubNoDrop() {
        return PerfCommon.parsePositiveEnv("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0
            ? 1 : 0;
    }

    private static ReadyEndpoint parseReadyEndpoint(String endpointArg) {
        String[] parts = endpointArg.split("\\|", -1);
        if (parts.length < 3) {
            return null;
        }
        String serverEndpoint = parts[0].trim();
        String registryPub = parts[1].trim();
        String registryRouter = parts[2].trim();
        if (serverEndpoint.isEmpty() || registryPub.isEmpty()
            || registryRouter.isEmpty()) {
            return null;
        }
        return new ReadyEndpoint(serverEndpoint, registryPub, registryRouter);
    }

    private record ReadyEndpoint(String serverEndpoint, String registryPub,
                                 String registryRouter) {
    }
}
