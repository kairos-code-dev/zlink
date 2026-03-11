/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiMetricHeader;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * MULTI_GATEWAY client benchmark using only high-level service APIs.
 */
public final class PerfMultiGatewayClient {
    private static final String PATTERN = "MULTI_GATEWAY";
    private static final String SERVICE_NAME = "perf-server";
    private static final String CLIENT_SERVICE_PREFIX = "c";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final int INITIAL_SETTLE_MS = 50;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfMultiGatewayClient() {
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

        int clients = PerfMultiCommon.resolveClients(PATTERN);
        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
        int rawPollTimeoutMs = PerfMultiCommon.resolveClientPollTimeoutMs();
        int pollTimeoutMs = Math.max(1, rawPollTimeoutMs);
        int latencySampleCap = PerfMultiCommon.resolveLatencySampleCap();
        int payloadSize = Math.max(msgSize, Math.max(MIN_PAYLOAD_BYTES,
            HEADER_BYTES));

        try (Context context = new Context();
             Discovery discovery = new Discovery(context, ServiceType.GATEWAY)) {
            PerfCommon.applyClientContextOptions(context);
            connectRegistryWithRetry(() ->
                discovery.connectRegistry(endpoint.registryRouter()));
            discovery.subscribe(SERVICE_NAME);

            List<ClientSlot> slots = new ArrayList<>(clients);
            try {
                for (int i = 0; i < clients; i++) {
                    slots.add(createClientSlot(context, discovery, transport,
                        endpoint.registryRouter(), i, connectTimeoutMs,
                        payloadSize));
                }

                if (!PerfCommon.waitUntil(
                    () -> discovery.receiverCount(SERVICE_NAME) > 0,
                    connectTimeoutMs,
                    10)) {
                    return 2;
                }

                if (!primeRoundtripAllSlots(slots, msgSize, connectTimeoutMs,
                    rawPollTimeoutMs)) {
                    System.err.println(
                        "ERROR,MULTI_GATEWAY,client,gateway_prime_failed");
                    return 2;
                }

                PerfCommon.sleepMillis(INITIAL_SETTLE_MS);

                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(latencySampleCap);
                PerfMultiMetricHeader.Header header =
                    new PerfMultiMetricHeader.Header();
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
                EchoPhaseState state = new EchoPhaseState(slots.size());

                long warmupDeadlineNs = System.nanoTime()
                    + (long) Math.max(0, warmupSeconds) * 1_000_000_000L;
                runEchoPhaseLoop(slots, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_WARMUP, warmupDeadlineNs, true,
                    false, header, reservoir, pollTimeoutMs, state);

                if (!runSettlePhase(slots, msgSize, runId, settleMs,
                    connectTimeoutMs, header, reservoir, pollTimeoutMs, state)) {
                    System.err.println(
                        "ERROR,MULTI_GATEWAY,client,settle_drain_incomplete");
                    return 2;
                }

                long benchDeadlineNs = System.nanoTime()
                    + (long) Math.max(1, durationSeconds) * 1_000_000_000L;
                long count = runEchoPhaseLoop(slots, msgSize, runId,
                    PerfMultiMetricHeader.PHASE_ACTIVE, benchDeadlineNs, true,
                    true, header, reservoir, pollTimeoutMs, state);

                sendStopToken(slots);

                if (count <= 0) {
                    System.err.println("ERROR,MULTI_GATEWAY,client,no_active_frames");
                    return 2;
                }

                double configuredSeconds = Math.max(1.0, durationSeconds);
                double throughput = count / configuredSeconds;
                PerfCommon.Stats stats = reservoir.snapshot();
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            } finally {
                closeSlots(slots);
            }
        } catch (RuntimeException ex) {
            logFailure("client", ex);
            return 2;
        }
    }

    private static ClientSlot createClientSlot(Context context,
                                               Discovery discovery,
                                               String transport,
                                               String registryRouterEndpoint,
                                               int index,
                                               int readyTimeoutMs,
                                               int payloadSize) {
        String serviceName = CLIENT_SERVICE_PREFIX + index;
        Receiver receiver = null;
        Gateway gateway = null;

        try {
            receiver = new Receiver(context, serviceName);
            applyReceiverOptions(receiver);
            PerfMultiTls.configureReceiverTlsServerIfNeeded(receiver, transport);

            String receiverEndpoint = PerfCommon.endpointFor(transport,
                "multi-gateway-client-" + index);
            receiver.bind(receiverEndpoint);
            connectReceiverRegistryWithRetry(receiver, registryRouterEndpoint);
            receiver.register(serviceName, receiverEndpoint, 1);
            if (!waitReceiverRegistered(receiver, serviceName, readyTimeoutMs)) {
                throw new IllegalStateException(
                    "gateway_client_receiver_register_not_ready");
            }

            gateway = new Gateway(context, discovery, serviceName);
            applyGatewayOptions(gateway);
            PerfMultiTls.configureGatewayTlsClientIfNeeded(gateway, transport);

            return new ClientSlot(receiver, gateway, payloadSize);
        } catch (RuntimeException ex) {
            closeQuietly(gateway, receiver);
            throw ex;
        }
    }

    private static boolean primeRoundtripAllSlots(List<ClientSlot> slots,
                                                  int msgSize,
                                                  int timeoutMs,
                                                  int pollTimeoutMs) {
        if (slots.isEmpty()) {
            return false;
        }

        long deadlineNs = System.nanoTime()
            + (long) Math.max(1000, timeoutMs) * 1_000_000L;
        boolean[] awaitingReply = new boolean[slots.size()];
        boolean[] sendPending = new boolean[slots.size()];
        byte[] roundtripCount = new byte[slots.size()];
        int primedCount = 0;
        int roundRobinIndex = 0;

        try (Poller receiverPoller = new Poller();
             Poller sendPoller = new Poller()) {
            for (int i = 0; i < slots.size(); i++) {
                receiverPoller.addReceiver(slots.get(i).receiver,
                    PollEventType.POLLIN.getValue(), Integer.valueOf(i));
                sendPoller.addGateway(slots.get(i).gateway, 0, Integer.valueOf(i));
            }

            while (primedCount < slots.size() && System.nanoTime() < deadlineNs) {
                int start = roundRobinIndex;
                for (int attempt = 0; attempt < slots.size(); attempt++) {
                    int index = (start + attempt) % slots.size();
                    if (!canStartPrimeRoundtrip(roundtripCount, awaitingReply,
                            sendPending, index, slots.size(), (byte) 1)) {
                        continue;
                    }
                    if (!tryPrimeSend(slots, msgSize, sendPoller, sendPending,
                            awaitingReply, roundtripCount, index)) {
                        return false;
                    }
                }
                roundRobinIndex = (start + 1) % slots.size();

                int receiverEvents = safePollCount(receiverPoller, pollTimeoutMs);
                for (int eventIndex = 0; eventIndex < receiverEvents; eventIndex++) {
                    if (!(receiverPoller.readyTag(eventIndex) instanceof Integer index)
                        || (receiverPoller.readyRevents(eventIndex)
                            & PollEventType.POLLIN.getValue()) == 0) {
                        continue;
                    }

                    int payloadLen = tryReceivePayload(slots.get(index));
                    if (payloadLen <= 0) {
                        continue;
                    }

                    awaitingReply[index] = false;
                    if (roundtripCount[index] < 1) {
                        roundtripCount[index]++;
                        if (roundtripCount[index] == 1) {
                            primedCount++;
                        }
                    }
                }

                int sendEvents = safePollCount(sendPoller, 0);
                for (int eventIndex = 0; eventIndex < sendEvents; eventIndex++) {
                    if (!(sendPoller.readyTag(eventIndex) instanceof Integer index)
                        || (sendPoller.readyRevents(eventIndex)
                            & PollEventType.POLLOUT.getValue()) == 0
                        || !canResumePrimeRoundtrip(roundtripCount, awaitingReply,
                            sendPending, index, slots.size(), (byte) 1)) {
                        continue;
                    }
                    if (!tryPrimeSend(slots, msgSize, sendPoller, sendPending,
                            awaitingReply, roundtripCount, index)) {
                        return false;
                    }
                }
            }
        }

        return primedCount == slots.size();
    }

    private static long runEchoPhaseLoop(List<ClientSlot> activeSlots,
                                         int msgSize,
                                         int runId,
                                         int phase,
                                         long deadlineNs,
                                         boolean allowSend,
                                         boolean collectMetric,
                                         PerfMultiMetricHeader.Header header,
                                         PerfCommon.LatencyReservoir reservoir,
                                         int pollTimeoutMs,
                                         EchoPhaseState state) {
        if (activeSlots.isEmpty()) {
            return 0L;
        }

        long matchedCount = 0L;
        try (Poller receiverPoller = new Poller();
             Poller sendPoller = new Poller()) {
            for (int i = 0; i < activeSlots.size(); i++) {
                receiverPoller.addReceiver(activeSlots.get(i).receiver,
                    PollEventType.POLLIN.getValue(), Integer.valueOf(i));
                sendPoller.addGateway(activeSlots.get(i).gateway, 0,
                    Integer.valueOf(i));
            }

            while (System.nanoTime() < deadlineNs) {
                if (allowSend) {
                    int start = state.roundRobinIndex;
                    for (int attempt = 0; attempt < activeSlots.size(); attempt++) {
                        int index = (start + attempt) % activeSlots.size();
                        if (!canStartSend(state, index)) {
                            continue;
                        }
                        tryGatewayPhaseSend(activeSlots, sendPoller, msgSize, runId,
                            phase, state, index);
                    }
                    state.roundRobinIndex = activeSlots.isEmpty() ? 0
                        : (start + 1) % activeSlots.size();
                }

                int receiverEvents = safePollCount(receiverPoller, pollTimeoutMs);
                for (int eventIndex = 0; eventIndex < receiverEvents; eventIndex++) {
                    if (!(receiverPoller.readyTag(eventIndex) instanceof Integer index)
                        || (receiverPoller.readyRevents(eventIndex)
                            & PollEventType.POLLIN.getValue()) == 0) {
                        continue;
                    }

                    int payloadLen = tryReceivePayload(activeSlots.get(index));
                    if (payloadLen <= 0) {
                        continue;
                    }

                    state.awaitingReply[index] = false;
                    if (collectMetric
                        && phase == PerfMultiMetricHeader.PHASE_ACTIVE
                        && payloadLen >= HEADER_BYTES
                        && PerfMultiMetricHeader.decodePayloadHeader(
                            activeSlots.get(index).recv, header)
                        && header.runId == runId
                        && header.phase == PerfMultiMetricHeader.PHASE_ACTIVE
                        && header.msgSize == msgSize) {
                        long nowUs = PerfMultiMetricHeader.nowUs();
                        reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
                        matchedCount++;
                    }

                    if (allowSend && canStartSend(state, index)) {
                        tryGatewayPhaseSend(activeSlots, sendPoller, msgSize, runId,
                            phase, state, index);
                    }
                }

                int sendEvents = safePollCount(sendPoller, 0);
                for (int eventIndex = 0; eventIndex < sendEvents; eventIndex++) {
                    if (!(sendPoller.readyTag(eventIndex) instanceof Integer index)
                        || (sendPoller.readyRevents(eventIndex)
                            & PollEventType.POLLOUT.getValue()) == 0
                        || !canResumeSend(state, index)) {
                        continue;
                    }
                    tryGatewayPhaseSend(activeSlots, sendPoller, msgSize, runId,
                        phase, state, index);
                }
            }
        }

        return matchedCount;
    }

    private static boolean runSettlePhase(List<ClientSlot> activeSlots,
                                          int msgSize,
                                          int runId,
                                          int settleMs,
                                          int connectTimeoutMs,
                                          PerfMultiMetricHeader.Header header,
                                          PerfCommon.LatencyReservoir reservoir,
                                          int pollTimeoutMs,
                                          EchoPhaseState state) {
        if (settleMs > 0) {
            long settleDeadlineNs = System.nanoTime() + (long) settleMs * 1_000_000L;
            runEchoPhaseLoop(activeSlots, msgSize, runId,
                PerfMultiMetricHeader.PHASE_WARMUP, settleDeadlineNs, false,
                false, header, reservoir, pollTimeoutMs, state);
        }

        if (!hasPendingReplies(state)) {
            return true;
        }

        long drainDeadlineNs = System.nanoTime()
            + (long) Math.max(connectTimeoutMs, Math.max(100, settleMs))
            * 1_000_000L;
        while (hasPendingReplies(state) && System.nanoTime() < drainDeadlineNs) {
            long sliceDeadlineNs = System.nanoTime() + 50_000_000L;
            if (sliceDeadlineNs > drainDeadlineNs) {
                sliceDeadlineNs = drainDeadlineNs;
            }
            runEchoPhaseLoop(activeSlots, msgSize, runId,
                PerfMultiMetricHeader.PHASE_WARMUP, sliceDeadlineNs, false,
                false, header, reservoir, pollTimeoutMs, state);
        }

        return !hasPendingReplies(state);
    }

    private static void tryGatewayPhaseSend(List<ClientSlot> activeSlots,
                                            Poller sendPoller,
                                            int msgSize,
                                            int runId,
                                            int phase,
                                            EchoPhaseState state,
                                            int index) {
        ClientSlot slot = activeSlots.get(index);
        if (!state.sendPending[index]) {
            PerfMultiMetricHeader.stampPayload(slot.payload, runId, phase,
                msgSize, state.seq++, PerfMultiMetricHeader.nowUs());
        }
        MemorySegment.copy(slot.payloadSegment, 0, slot.message.dataSegment(), 0,
            slot.payload.length);

        while (true) {
            try {
                slot.gateway.sendTo(SERVICE_NAME, slot.message, SendFlag.DONTWAIT);
                state.sendPending[index] = false;
                sendPoller.modifyGateway(slot.gateway, 0);
                state.awaitingReply[index] = true;
                return;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isWouldBlock(errno)) {
                    state.sendPending[index] = true;
                    sendPoller.modifyGateway(slot.gateway,
                        PollEventType.POLLOUT.getValue());
                    return;
                }
                throw ex;
            }
        }
    }

    private static int tryReceivePayload(ClientSlot slot) {
        while (true) {
            try {
                return slot.receiver.recvSinglePayload(slot.recvSegment,
                    ReceiveFlag.DONTWAIT);
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isWouldBlock(errno)) {
                    return 0;
                }
                throw ex;
            }
        }
    }

    private static boolean tryPrimeSend(List<ClientSlot> slots,
                                        int msgSize,
                                        Poller sendPoller,
                                        boolean[] sendPending,
                                        boolean[] awaitingReply,
                                        byte[] roundtripCount,
                                        int index) {
        if (index < 0 || index >= slots.size()) {
            return false;
        }
        if (roundtripCount[index] >= 1 || awaitingReply[index]) {
            return true;
        }

        ClientSlot slot = slots.get(index);
        if (!sendPending[index]) {
            PerfMultiMetricHeader.stampPayload(slot.payload, 1,
                PerfMultiMetricHeader.PHASE_DRAIN, msgSize, index + 1L,
                PerfMultiMetricHeader.nowUs());
        }
        MemorySegment.copy(slot.payloadSegment, 0, slot.message.dataSegment(), 0,
            slot.payload.length);

        while (true) {
            try {
                slot.gateway.sendTo(SERVICE_NAME, slot.message, SendFlag.DONTWAIT);
                sendPending[index] = false;
                sendPoller.modifyGateway(slot.gateway, 0);
                awaitingReply[index] = true;
                return true;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isWouldBlock(errno)) {
                    sendPending[index] = true;
                    sendPoller.modifyGateway(slot.gateway,
                        PollEventType.POLLOUT.getValue());
                    return true;
                }
                return false;
            }
        }
    }

    private static void sendStopToken(List<ClientSlot> activeSlots) {
        if (activeSlots.isEmpty()) {
            return;
        }

        try (Message stopMessage = Message.fromBytes(STOP_TOKEN, 0,
                 STOP_TOKEN.length)) {
            activeSlots.get(0).gateway.sendTo(SERVICE_NAME, stopMessage,
                SendFlag.DONTWAIT);
        }
    }

    private static boolean waitReceiverRegistered(Receiver receiver,
                                                  String serviceName,
                                                  int timeoutMs) {
        long deadlineNs = System.nanoTime()
            + (long) Math.max(1, timeoutMs) * 1_000_000L;
        while (System.nanoTime() < deadlineNs) {
            if (receiver.registerResult(serviceName).status() == 0) {
                return true;
            }
            PerfCommon.sleepMillis(10);
        }
        return false;
    }

    private static void connectRegistryWithRetry(Runnable connect) {
        RuntimeException last = null;
        long deadlineNs = System.nanoTime() + 5_000_000_000L;
        while (System.nanoTime() < deadlineNs) {
            try {
                connect.run();
                return;
            } catch (ZlinkException ex) {
                if (!isInterrupted(ex.errno()) && !isWouldBlock(ex.errno())) {
                    throw ex;
                }
                last = ex;
            }
            PerfCommon.sleepMillis(20);
        }

        if (last != null) {
            throw last;
        }
        throw new IllegalStateException("registry connect timeout");
    }

    private static void connectReceiverRegistryWithRetry(Receiver receiver,
                                                         String endpoint) {
        connectRegistryWithRetry(() -> receiver.connectRegistry(endpoint));
    }

    private static void applyReceiverOptions(Receiver receiver) {
        receiver.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        receiver.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        receiver.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        receiver.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        receiver.setOption(SocketOptions.LINGER, 0);
    }

    private static void applyGatewayOptions(Gateway gateway) {
        gateway.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        gateway.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        gateway.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        gateway.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        gateway.setOption(SocketOptions.LINGER, 0);
    }

    private static ReadyEndpoint parseReadyEndpoint(String endpointArg) {
        String[] parts = endpointArg.split("\\|", -1);
        if (parts.length < 3) {
            return null;
        }
        String registryRouter = parts[2].trim();
        if (registryRouter.isEmpty()) {
            return null;
        }
        return new ReadyEndpoint(registryRouter);
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static void closeSlots(List<ClientSlot> slots) {
        for (ClientSlot slot : slots) {
            if (slot != null) {
                slot.close();
            }
        }
    }

    private static void closeQuietly(AutoCloseable... resources) {
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

    private static void logFailure(String role, RuntimeException ex) {
        String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
            : ex.getMessage();
        System.err.println("multi_gateway_" + role + "_error:" + message);
    }

    private static int safePollCount(Poller poller, int timeoutMs) {
        try {
            return poller.pollCount(timeoutMs);
        } catch (ZlinkException ex) {
            if (isInterrupted(ex.errno())) {
                return 0;
            }
            throw ex;
        }
    }

    private static boolean hasPendingReplies(EchoPhaseState state) {
        for (boolean awaiting : state.awaitingReply) {
            if (awaiting) {
                return true;
            }
        }
        for (boolean pending : state.sendPending) {
            if (pending) {
                return true;
            }
        }
        return false;
    }

    private static boolean canStartSend(EchoPhaseState state, int index) {
        return !state.awaitingReply[index] && !state.sendPending[index];
    }

    private static boolean canResumeSend(EchoPhaseState state, int index) {
        return !state.awaitingReply[index] && state.sendPending[index];
    }

    private static boolean canStartPrimeRoundtrip(byte[] roundtripCount,
                                                  boolean[] awaitingReply,
                                                  boolean[] sendPending,
                                                  int index,
                                                  int slotCount,
                                                  byte targetRoundtrips) {
        return index >= 0 && index < slotCount
            && roundtripCount[index] < targetRoundtrips
            && !awaitingReply[index]
            && !sendPending[index];
    }

    private static boolean canResumePrimeRoundtrip(byte[] roundtripCount,
                                                   boolean[] awaitingReply,
                                                   boolean[] sendPending,
                                                   int index,
                                                   int slotCount,
                                                   byte targetRoundtrips) {
        return index >= 0 && index < slotCount
            && roundtripCount[index] < targetRoundtrips
            && !awaitingReply[index]
            && sendPending[index];
    }

    private record ReadyEndpoint(String registryRouter) {
    }

    private static final class ClientSlot implements AutoCloseable {
        private final Receiver receiver;
        private final Gateway gateway;
        private final byte[] payload;
        private final MemorySegment payloadSegment;
        private final Message message;
        private final byte[] recv;
        private final MemorySegment recvSegment;

        private ClientSlot(Receiver receiver, Gateway gateway, int payloadSize) {
            this.receiver = receiver;
            this.gateway = gateway;
            this.payload = new byte[payloadSize];
            this.payloadSegment = MemorySegment.ofArray(this.payload);
            this.message = new Message(payloadSize);
            this.recv = new byte[payloadSize];
            this.recvSegment = MemorySegment.ofArray(this.recv);
        }

        @Override
        public void close() {
            closeQuietly(gateway, message, receiver);
        }
    }

    private static final class EchoPhaseState {
        private final boolean[] awaitingReply;
        private final boolean[] sendPending;
        private long seq;
        private int roundRobinIndex;

        private EchoPhaseState(int slotCount) {
            this.awaitingReply = new boolean[Math.max(0, slotCount)];
            this.sendPending = new boolean[Math.max(0, slotCount)];
            this.seq = 1L;
            this.roundRobinIndex = 0;
        }
    }
}
