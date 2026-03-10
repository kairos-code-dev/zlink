/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.Socket;
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
 * MULTI_GATEWAY client benchmark.
 * Gateway sends requests, per-client receiver instances are pollable.
 */
public final class PerfMultiGatewayClient {
    private static final String PATTERN = "MULTI_GATEWAY";
    private static final String SERVICE_NAME = "perf-server";
    private static final String CLIENT_SERVICE_PREFIX = "c";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;
    private static final int ROUTING_ID_BUFFER_BYTES = 256;
    private static final int INITIAL_SETTLE_MS = 50;
    private static final int PENDING_TAG_BASE = 1_000_000;
    private static final long NANOSECONDS_PER_SECOND = 1_000_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class PendingSend {
        boolean pending;
        final byte[] payload;
        final MemorySegment payloadSegment;
        final Message message;

        PendingSend(int payloadSize) {
            this.payload = new byte[payloadSize];
            this.payloadSegment = MemorySegment.ofArray(this.payload);
            this.message = new Message(payloadSize);
        }
    }

    private static final class PrimeResult {
        long nextSeq = -1L;
        long immediateSendCount = 0L;
        long flushedSendCount = 0L;
        long recvCount = 0L;
        long matchedCount = 0L;

        boolean success() {
            return nextSeq > 0L;
        }
    }

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
        int pollTimeoutMs = PerfMultiCommon.resolveClientPollTimeoutMs();
        int payloadSize = Math.max(msgSize, MIN_PAYLOAD_BYTES);

        try (Context context = new Context();
             Discovery discovery = new Discovery(context, ServiceType.GATEWAY)) {
            PerfCommon.applyClientContextOptions(context);

            discovery.connectRegistry(endpoint.registryPub());
            discovery.subscribe(SERVICE_NAME);

            List<ClientSlot> slots = new ArrayList<>(clients);
            List<ClientSlot> activeSlots = new ArrayList<>(clients);
            try {
                for (int i = 0; i < clients; i++) {
                    slots.add(createClientSlot(context, discovery, transport,
                        endpoint.registryRouter(), i, connectTimeoutMs));
                }
                if (slots.isEmpty()) {
                    return 2;
                }

                if (!PerfCommon.waitUntil(
                    () -> discovery.receiverCount(SERVICE_NAME) > 0,
                    connectTimeoutMs,
                    10)) {
                    return 2;
                }

                if (!collectActiveSlots(slots, activeSlots, connectTimeoutMs)) {
                    return 2;
                }
                PerfCommon.sleepMillis(INITIAL_SETTLE_MS);

                Poller poller = new Poller();
                for (int i = 0; i < activeSlots.size(); i++) {
                    poller.addReceiver(activeSlots.get(i).receiver,
                        PollEventType.POLLIN.getValue(), Integer.valueOf(i));
                    poller.addGateway(activeSlots.get(i).gateway,
                        0,
                        Integer.valueOf(activeTag(i)));
                }

                byte[] recv = new byte[payloadSize];
                byte[] routingId = new byte[ROUTING_ID_BUFFER_BYTES];
                PendingSend[] pendingSends = new PendingSend[activeSlots.size()];
                for (int i = 0; i < pendingSends.length; i++) {
                    pendingSends[i] = new PendingSend(payloadSize);
                }
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
                long seq = 1;
                int index = 0;
                boolean[] awaitingReply = new boolean[activeSlots.size()];

                try (Message stopMessage = Message.fromBytes(STOP_TOKEN, 0,
                         STOP_TOKEN.length)) {
                    PerfCommon.LatencyReservoir reservoir =
                        new PerfCommon.LatencyReservoir(
                            PerfMultiCommon.resolveLatencySampleCap());
                    PerfMultiMetricHeader.Header header =
                        new PerfMultiMetricHeader.Header();

                    PrimeResult prime = primeRoundTrips(poller, activeSlots, pendingSends,
                        awaitingReply, routingId, recv, runId, msgSize,
                        connectTimeoutMs, pollTimeoutMs, seq, header);
                    if (!prime.success()) {
                        StringBuilder connections = new StringBuilder();
                        for (int i = 0; i < activeSlots.size(); i++) {
                            if (i > 0) {
                                connections.append('|');
                            }
                            connections.append(activeSlots.get(i)
                                .gateway.connectionCount(SERVICE_NAME));
                        }
                        System.err.println("ERROR,MULTI_GATEWAY,client,prime_failed,"
                            + "send_immediate=" + prime.immediateSendCount + ","
                            + "send_flushed=" + prime.flushedSendCount + ","
                            + "recv=" + prime.recvCount + ","
                            + "matched=" + prime.matchedCount + ","
                            + "connections=" + connections);
                        return 2;
                    }
                    seq = prime.nextSeq;
                    PerfCommon.sleepMillis(INITIAL_SETTLE_MS);

                    // --- Warmup ---
                    long warmupDeadline = System.nanoTime()
                        + (long) Math.max(0, warmupSeconds)
                        * NANOSECONDS_PER_SECOND;
                    while (System.nanoTime() < warmupDeadline) {
                        boolean progressed = false;

                        ClientSlot slot = activeSlots.get(index);
                        PendingSend pending = pendingSends[index];
                        if (!awaitingReply[index] && !pending.pending) {
                            PerfMultiMetricHeader.stampPayload(pending.payload, runId,
                                PerfMultiMetricHeader.PHASE_WARMUP, msgSize, seq++,
                                PerfMultiMetricHeader.nowUs());
                            progressed = tryStartSend(poller, slot, index,
                                pending, awaitingReply);
                        }
                        index = (index + 1) % activeSlots.size();

                        DrainResult warmupDrain = drainReplies(poller,
                            activeSlots, pendingSends, awaitingReply, routingId,
                            recv, runId, msgSize, pollTimeoutMs, false, header,
                            reservoir);
                        progressed = progressed || warmupDrain.progressed();
                    }

                    // --- Settle ---
                    if (settleMs > 0) {
                        PerfCommon.sleepMillis(settleMs);
                    }

                    // --- Active measurement ---
                    long count = 0;
                    long benchDeadline = System.nanoTime()
                        + (long) Math.max(1, durationSeconds)
                        * NANOSECONDS_PER_SECOND;
                    long benchBegin = System.nanoTime();

                    while (System.nanoTime() < benchDeadline) {
                        boolean progressed = false;

                        ClientSlot slot = activeSlots.get(index);
                        PendingSend pending = pendingSends[index];
                        if (!awaitingReply[index] && !pending.pending) {
                            PerfMultiMetricHeader.stampPayload(pending.payload, runId,
                                PerfMultiMetricHeader.PHASE_ACTIVE, msgSize, seq++,
                                PerfMultiMetricHeader.nowUs());
                            progressed = tryStartSend(poller, slot, index,
                                pending, awaitingReply);
                        }
                        index = (index + 1) % activeSlots.size();

                        DrainResult activeDrain = drainReplies(poller,
                            activeSlots, pendingSends, awaitingReply, routingId,
                            recv, runId, msgSize, pollTimeoutMs, true, header,
                            reservoir);
                        count += activeDrain.matchedCount();
                        progressed = progressed || activeDrain.progressed();
                    }

                    activeSlots.get(0).gateway.sendTo(SERVICE_NAME, stopMessage,
                        SendFlag.NONE);

                    if (count <= 0) {
                        System.err.println("ERROR,MULTI_GATEWAY,client,no_active_frames");
                        return 2;
                    }
                    double elapsedSec = Math.max(1e-9,
                        (System.nanoTime() - benchBegin) / 1_000_000_000.0);
                    double throughput = count / elapsedSec;
                    PerfCommon.Stats stats = reservoir.snapshot();
                    PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                        stats.meanUs(), stats.p95Us(), stats.p99Us());
                    return 0;
                } finally {
                    closePendingSends(pendingSends);
                }
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
                                               int readyTimeoutMs) {
        String serviceName = CLIENT_SERVICE_PREFIX + index;
        Receiver receiver = null;
        Socket receiverRouter = null;
        Gateway gateway = null;

        try {
            receiver = new Receiver(context, serviceName);
            applyReceiverOptions(receiver);
            PerfMultiTls.configureReceiverTlsServerIfNeeded(receiver, transport);

            String receiverEndpoint = PerfCommon.endpointFor(transport,
                "multi-gateway-client-" + index);
            receiver.bind(receiverEndpoint);
            receiver.connectRegistry(registryRouterEndpoint);
            receiver.register(serviceName, receiverEndpoint, 1);
            if (!waitReceiverRegistered(receiver, serviceName, readyTimeoutMs)) {
                throw new IllegalStateException(
                    "gateway_client_receiver_register_not_ready");
            }

            receiverRouter = receiver.routerSocket();
            applyReceiverRouterOptions(receiverRouter, serviceName);

            gateway = new Gateway(context, discovery, serviceName);
            applyGatewayOptions(gateway);
            PerfMultiTls.configureGatewayTlsClientIfNeeded(gateway, transport);

            return new ClientSlot(serviceName, receiver, receiverRouter, gateway);
        } catch (RuntimeException ex) {
            closeQuietly(gateway, receiverRouter, receiver);
            throw ex;
        }
    }

    private static boolean collectActiveSlots(List<ClientSlot> slots,
                                              List<ClientSlot> activeSlots,
                                              int timeoutMs) {
        long deadlineNs = System.nanoTime()
            + (long) Math.max(1, timeoutMs) * 1_000_000L;
        boolean[] selected = new boolean[slots.size()];
        int selectedCount = 0;

        while (System.nanoTime() < deadlineNs && selectedCount < slots.size()) {
            for (int i = 0; i < slots.size(); i++) {
                if (selected[i]) {
                    continue;
                }
                if (slots.get(i).gateway.connectionCount(SERVICE_NAME) > 0) {
                    selected[i] = true;
                    selectedCount++;
                    activeSlots.add(slots.get(i));
                }
            }
            if (selectedCount < slots.size()) {
                PerfCommon.sleepMillis(10);
            }
        }
        return selectedCount == slots.size() && !activeSlots.isEmpty();
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

    private static PrimeResult primeRoundTrips(Poller poller,
                                               List<ClientSlot> activeSlots,
                                               PendingSend[] pendingSends,
                                               boolean[] awaitingReply,
                                               byte[] routingIdBuffer,
                                               byte[] recvBuffer,
                                               int runId,
                                               int msgSize,
                                               int timeoutMs,
                                               int pollTimeoutMs,
                                               long startingSeq,
                                               PerfMultiMetricHeader.Header header) {
        PrimeResult result = new PrimeResult();
        if (activeSlots.isEmpty()) {
            return result;
        }

        long seq = startingSeq;
        long deadlineNs = System.nanoTime()
            + (long) Math.max(timeoutMs, 1000) * 1_000_000L;
        boolean[] primed = new boolean[activeSlots.size()];
        int primedCount = 0;
        int sendIndex = 0;

        while (System.nanoTime() < deadlineNs && primedCount < activeSlots.size()) {
            for (int i = 0; i < activeSlots.size(); i++) {
                int slotIndex = (sendIndex + i) % activeSlots.size();
                if (primed[slotIndex]
                    || awaitingReply[slotIndex]
                    || pendingSends[slotIndex].pending) {
                    continue;
                }
                PerfMultiMetricHeader.stampPayload(pendingSends[slotIndex].payload,
                    runId, PerfMultiMetricHeader.PHASE_DRAIN, msgSize, seq++,
                    PerfMultiMetricHeader.nowUs());
                if (tryStartSend(poller, activeSlots.get(slotIndex), slotIndex,
                    pendingSends[slotIndex], awaitingReply)) {
                    result.immediateSendCount++;
                }
            }
            sendIndex = (sendIndex + 1) % activeSlots.size();

            int eventCount = poller.pollCount(pollTimeoutMs);
            for (int i = 0; i < eventCount; i++) {
                Object tag = poller.readyTag(i);
                if (!(tag instanceof Integer)) {
                    continue;
                }
                int rawIndex = (Integer) tag;
                int pendingIndex = pendingIndexFromTag(rawIndex);
                if (pendingIndex >= 0) {
                    if (pendingSends[pendingIndex].pending) {
                        if (tryFlushPending(poller, activeSlots.get(pendingIndex),
                            pendingIndex, pendingSends[pendingIndex], awaitingReply)) {
                            result.flushedSendCount++;
                        }
                    }
                    continue;
                }

                int slotIndex = rawIndex;
                ClientSlot slot = activeSlots.get(slotIndex);
                while (true) {
                    int payloadLen = receiveReceiverPayloadNonBlocking(
                        slot.receiverRouter, routingIdBuffer, recvBuffer);
                    if (payloadLen <= 0) {
                        break;
                    }
                    result.recvCount++;
                    awaitingReply[slotIndex] = false;
                    if (primed[slotIndex]) {
                        continue;
                    }
                    if (payloadLen < HEADER_BYTES
                        || !PerfMultiMetricHeader.decodePayloadHeader(recvBuffer,
                        header)
                        || header.runId != runId
                        || header.phase != PerfMultiMetricHeader.PHASE_DRAIN
                        || header.msgSize != msgSize) {
                        continue;
                    }
                    primed[slotIndex] = true;
                    primedCount++;
                    result.matchedCount++;
                }
            }
        }

        if (primedCount == activeSlots.size()) {
            result.nextSeq = seq;
        }
        return result;
    }

    private static DrainResult drainReplies(Poller poller,
                                            List<ClientSlot> activeSlots,
                                            PendingSend[] pendingSends,
                                            boolean[] awaitingReply,
                                            byte[] routingIdBuffer,
                                            byte[] recvBuffer,
                                            int runId,
                                            int msgSize,
                                            int pollTimeoutMs,
                                            boolean collectMetric,
                                            PerfMultiMetricHeader.Header header,
                                            PerfCommon.LatencyReservoir reservoir) {
        boolean progressed = false;
        long matchedCount = 0;

        int eventCount = poller.pollCount(pollTimeoutMs);
        for (int i = 0; i < eventCount; i++) {
            Object tag = poller.readyTag(i);
            if (!(tag instanceof Integer)) {
                continue;
            }
            int rawIndex = (Integer) tag;
            int pendingIndex = pendingIndexFromTag(rawIndex);
            if (pendingIndex >= 0) {
                if (pendingSends[pendingIndex].pending) {
                    progressed |= tryFlushPending(poller,
                        activeSlots.get(pendingIndex), pendingIndex,
                        pendingSends[pendingIndex], awaitingReply);
                }
                continue;
            }
            int socketIndex = rawIndex;
            ClientSlot slot = activeSlots.get(socketIndex);
            while (true) {
                int n = receiveReceiverPayloadNonBlocking(slot.receiverRouter,
                    routingIdBuffer, recvBuffer);
                if (n <= 0) {
                    break;
                }
                progressed = true;
                awaitingReply[socketIndex] = false;
                if (!collectMetric) {
                    continue;
                }
                if (n >= HEADER_BYTES
                    && PerfMultiMetricHeader.decodePayloadHeader(recvBuffer, header)
                    && header.runId == runId
                    && header.phase == PerfMultiMetricHeader.PHASE_ACTIVE
                    && header.msgSize == msgSize) {
                    long nowUs = PerfMultiMetricHeader.nowUs();
                    reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
                    matchedCount++;
                }
            }
        }

        return new DrainResult(progressed, matchedCount);
    }

    private static boolean tryStartSend(Poller poller, ClientSlot slot,
                                        int index, PendingSend pending,
                                        boolean[] awaitingReply) {
        if (trySendNonBlocking(slot, pending)) {
            awaitingReply[index] = true;
            return true;
        }
        if (!pending.pending) {
            pending.pending = true;
            poller.modifyGateway(slot.gateway,
                PollEventType.POLLOUT.getValue());
        }
        return false;
    }

    private static boolean tryFlushPending(Poller poller, ClientSlot slot,
                                           int index, PendingSend pending,
                                           boolean[] awaitingReply) {
        if (!pending.pending) {
            return false;
        }
        if (!trySendNonBlocking(slot, pending)) {
            return false;
        }
        pending.pending = false;
        awaitingReply[index] = true;
        poller.modifyGateway(slot.gateway, 0);
        return true;
    }

    private static boolean trySendNonBlocking(ClientSlot slot,
                                              PendingSend pending) {
        MemorySegment.copy(pending.payloadSegment, 0,
            pending.message.dataSegment(), 0, pending.payload.length);
        while (true) {
            try {
                slot.gateway.sendTo(SERVICE_NAME, pending.message,
                    SendFlag.DONTWAIT);
                return true;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isWouldBlock(errno)) {
                    return false;
                }
                throw ex;
            }
        }
    }

    private static int activeTag(int index) {
        return PENDING_TAG_BASE + index;
    }

    private static int pendingIndexFromTag(int rawTag) {
        if (rawTag < PENDING_TAG_BASE) {
            return -1;
        }
        return rawTag - PENDING_TAG_BASE;
    }

    private static int receiveReceiverPayloadNonBlocking(Socket receiverRouter,
                                                         byte[] routingIdBuffer,
                                                         byte[] payloadBuffer) {
        int ridLen = receiveNonBlocking(receiverRouter, routingIdBuffer);
        if (ridLen <= 0) {
            return 0;
        }
        if (getRcvMore(receiverRouter) == 0) {
            return 0;
        }

        int payloadLen = receiveNonBlocking(receiverRouter, payloadBuffer);
        if (payloadLen <= 0) {
            throw new IllegalStateException("gateway_partial_message");
        }

        while (getRcvMore(receiverRouter) != 0) {
            int drained = receiveNonBlocking(receiverRouter, payloadBuffer);
            if (drained <= 0) {
                throw new IllegalStateException("gateway_partial_message");
            }
            payloadLen = drained;
        }
        return payloadLen;
    }

    private static int receiveNonBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.DONTWAIT);
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

    private static int getRcvMore(Socket socket) {
        Integer value = socket.getOption(SocketOptions.RCVMORE);
        return value == null ? 0 : value;
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

    private static void applyReceiverRouterOptions(Socket receiverRouter,
                                                   String serviceName) {
        receiverRouter.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        receiverRouter.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        receiverRouter.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        receiverRouter.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        receiverRouter.setOption(SocketOptions.LINGER, 0);
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
        String registryPub = parts[1].trim();
        String registryRouter = parts[2].trim();
        if (registryPub.isEmpty() || registryRouter.isEmpty()) {
            return null;
        }
        return new ReadyEndpoint(registryPub, registryRouter);
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static void closeSlots(List<ClientSlot> slots) {
        for (ClientSlot slot : slots) {
            if (slot == null) {
                continue;
            }
            slot.close();
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

    private static void closePendingSends(PendingSend[] pendingSends) {
        if (pendingSends == null) {
            return;
        }
        for (PendingSend pending : pendingSends) {
            if (pending == null) {
                continue;
            }
            pending.message.close();
        }
    }

    private static void logFailure(String role, RuntimeException ex) {
        String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
            : ex.getMessage();
        System.err.println("multi_gateway_" + role + "_error:" + message);
    }

    private record ReadyEndpoint(String registryPub, String registryRouter) {
    }

    private record DrainResult(boolean progressed, long matchedCount) {
    }

    private static final class ClientSlot implements AutoCloseable {
        private final String serviceName;
        private final Receiver receiver;
        private final Socket receiverRouter;
        private final Gateway gateway;

        private ClientSlot(String serviceName, Receiver receiver,
                           Socket receiverRouter, Gateway gateway) {
            this.serviceName = serviceName;
            this.receiver = receiver;
            this.receiverRouter = receiverRouter;
            this.gateway = gateway;
        }

        @Override
        public void close() {
            closeQuietly(gateway, receiverRouter, receiver);
        }
    }
}
