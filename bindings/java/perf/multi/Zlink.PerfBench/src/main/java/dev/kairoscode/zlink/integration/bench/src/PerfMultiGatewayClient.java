/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
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

                List<ClientSlot> activeSlots = collectReadySlots(slots,
                    connectTimeoutMs);
                activeSlots = collectEchoReadySlots(activeSlots, msgSize,
                    connectTimeoutMs);
                if (activeSlots.isEmpty()) {
                    System.err.println("ERROR,MULTI_GATEWAY,client,no_ready_connections");
                    return 2;
                }

                PerfCommon.sleepMillis(INITIAL_SETTLE_MS);

                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(latencySampleCap);
                PerfMultiMetricHeader.Header header =
                    new PerfMultiMetricHeader.Header();
                int runId = (int) (PerfMultiMetricHeader.nowUs() & 0x7FFF_FFFFL);
                long seq = 1L;

                long warmupDeadlineNs = System.nanoTime()
                    + (long) Math.max(0, warmupSeconds) * 1_000_000_000L;
                while (System.nanoTime() < warmupDeadlineNs) {
                    seq = runRoundTripBatch(activeSlots, msgSize, runId,
                        PerfMultiMetricHeader.PHASE_WARMUP, seq,
                        warmupDeadlineNs, false, header, reservoir).nextSeq();
                }

                if (settleMs > 0) {
                    PerfCommon.sleepMillis(settleMs);
                }

                long benchBeginNs = System.nanoTime();
                long benchDeadlineNs = benchBeginNs
                    + (long) Math.max(1, durationSeconds) * 1_000_000_000L;
                long count = 0L;
                while (System.nanoTime() < benchDeadlineNs) {
                    BatchResult batch = runRoundTripBatch(activeSlots, msgSize,
                        runId, PerfMultiMetricHeader.PHASE_ACTIVE, seq,
                        benchDeadlineNs, true, header, reservoir);
                    seq = batch.nextSeq();
                    count += batch.matchedCount();
                }

                sendStopToken(activeSlots);

                if (count <= 0) {
                    System.err.println("ERROR,MULTI_GATEWAY,client,no_active_frames");
                    return 2;
                }

                double elapsedSec = Math.max(1e-9,
                    (System.nanoTime() - benchBeginNs) / 1_000_000_000.0);
                double throughput = count / elapsedSec;
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

    private static List<ClientSlot> collectReadySlots(List<ClientSlot> slots,
                                                      int timeoutMs) {
        List<ClientSlot> activeSlots = new ArrayList<>(slots.size());
        long deadlineNs = System.nanoTime()
            + (long) Math.max(1, timeoutMs) * 1_000_000L;
        boolean[] selected = new boolean[slots.size()];
        int selectedCount = 0;

        while (System.nanoTime() < deadlineNs && selectedCount < slots.size()) {
            for (int i = 0; i < slots.size(); i++) {
                if (selected[i]) {
                    continue;
                }
                if (safeConnectionCount(slots.get(i).gateway) <= 0) {
                    continue;
                }
                selected[i] = true;
                selectedCount++;
                activeSlots.add(slots.get(i));
            }
            if (selectedCount < slots.size()) {
                PerfCommon.sleepMillis(10);
            }
        }

        return activeSlots;
    }

    private static List<ClientSlot> collectEchoReadySlots(List<ClientSlot> slots,
                                                          int msgSize,
                                                          int timeoutMs) {
        List<ClientSlot> readySlots = new ArrayList<>(slots.size());
        if (slots.isEmpty()) {
            return readySlots;
        }

        PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();
        for (ClientSlot slot : slots) {
            long deadlineNs = System.nanoTime()
                + (long) Math.max(1, timeoutMs) * 1_000_000L;
            while (System.nanoTime() < deadlineNs) {
                if (safeConnectionCount(slot.gateway) <= 0) {
                    PerfCommon.sleepMillis(1);
                    continue;
                }
                PerfMultiMetricHeader.stampPayload(slot.payload, 1,
                    PerfMultiMetricHeader.PHASE_DRAIN, msgSize, 1L,
                    PerfMultiMetricHeader.nowUs());
                if (!trySendPayloadUntilReady(slot, deadlineNs)) {
                    continue;
                }
                int payloadLen = receivePayloadUntilReady(slot.receiver, slot.recv,
                    deadlineNs);
                if (payloadLen >= HEADER_BYTES
                    && PerfMultiMetricHeader.decodePayloadHeader(slot.recv, header)
                    && header.runId == 1
                    && header.phase == PerfMultiMetricHeader.PHASE_DRAIN
                    && header.msgSize == msgSize) {
                    readySlots.add(slot);
                    break;
                }
            }
        }

        return readySlots;
    }

    private static BatchResult runRoundTripBatch(List<ClientSlot> activeSlots,
                                                 int msgSize,
                                                 int runId,
                                                 int phase,
                                                 long startingSeq,
                                                 long deadlineNs,
                                                 boolean collectMetric,
                                                 PerfMultiMetricHeader.Header header,
                                                 PerfCommon.LatencyReservoir reservoir) {
        long seq = startingSeq;
        long matchedCount = 0L;

        for (ClientSlot slot : activeSlots) {
            if (System.nanoTime() >= deadlineNs) {
                break;
            }

            PerfMultiMetricHeader.stampPayload(slot.payload, runId, phase,
                msgSize, seq++, PerfMultiMetricHeader.nowUs());
            if (!trySendPayloadUntilReady(slot, deadlineNs)) {
                continue;
            }

            int payloadLen = receivePayloadUntilReady(slot.receiver, slot.recv,
                deadlineNs);
            if (!collectMetric || phase != PerfMultiMetricHeader.PHASE_ACTIVE) {
                continue;
            }
            if (payloadLen < HEADER_BYTES
                || !PerfMultiMetricHeader.decodePayloadHeader(slot.recv, header)
                || header.runId != runId
                || header.phase != PerfMultiMetricHeader.PHASE_ACTIVE
                || header.msgSize != msgSize) {
                continue;
            }

            long nowUs = PerfMultiMetricHeader.nowUs();
            reservoir.add(Math.max(0L, nowUs - header.sentTsUs));
            matchedCount++;
        }

        return new BatchResult(seq, matchedCount);
    }

    private static boolean trySendPayloadUntilReady(ClientSlot slot,
                                                    long deadlineNs) {
        MemorySegment.copy(slot.payloadSegment, 0, slot.message.dataSegment(), 0,
            slot.payload.length);
        while (System.nanoTime() < deadlineNs) {
            try {
                slot.gateway.sendTo(SERVICE_NAME, slot.message, SendFlag.DONTWAIT);
                return true;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (!isWouldBlock(errno)) {
                    throw ex;
                }
            }
            PerfCommon.sleepMillis(1);
        }

        return false;
    }

    private static int receivePayloadUntilReady(Receiver receiver,
                                                byte[] payloadBuffer,
                                                long deadlineNs) {
        while (System.nanoTime() < deadlineNs) {
            try (Receiver.ReceiverMessages received =
                     receiver.recv(ReceiveFlag.DONTWAIT)) {
                Message[] parts = received.parts();
                if (parts.length == 0) {
                    return 0;
                }
                return parts[parts.length - 1].copyTo(payloadBuffer);
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (!isWouldBlock(errno)) {
                    throw ex;
                }
            }
            PerfCommon.sleepMillis(1);
        }

        return 0;
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

    private static int safeConnectionCount(Gateway gateway) {
        try {
            return gateway.connectionCount(SERVICE_NAME);
        } catch (ZlinkException ex) {
            if (isInterrupted(ex.errno()) || isWouldBlock(ex.errno())) {
                return 0;
            }
            throw ex;
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

    private record ReadyEndpoint(String registryRouter) {
    }

    private record BatchResult(long nextSeq, long matchedCount) {
    }

    private static final class ClientSlot implements AutoCloseable {
        private final Receiver receiver;
        private final Gateway gateway;
        private final byte[] payload;
        private final MemorySegment payloadSegment;
        private final Message message;
        private final byte[] recv;

        private ClientSlot(Receiver receiver, Gateway gateway, int payloadSize) {
            this.receiver = receiver;
            this.gateway = gateway;
            this.payload = new byte[payloadSize];
            this.payloadSegment = MemorySegment.ofArray(this.payload);
            this.message = new Message(payloadSize);
            this.recv = new byte[payloadSize];
        }

        @Override
        public void close() {
            closeQuietly(gateway, message, receiver);
        }
    }
}
