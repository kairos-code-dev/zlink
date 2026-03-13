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
import dev.kairoscode.zlink.service.registry.Registry;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;

/**
 * MULTI_GATEWAY server benchmark using only high-level service APIs.
 */
public final class PerfMultiGatewayServer {
    private static final String PATTERN = "MULTI_GATEWAY";
    private static final String SERVICE_NAME = "perf-server";
    private static final String SERVER_RECEIVER_ROUTING_ID = "perf-server-rx";
    private static final String CLIENT_SERVICE_PREFIX = "c";
    private static final String SERVER_GATEWAY_ROUTING_ID = "sg";
    private static final int REGISTRY_FIXED_PORT_OFFSET = 64;
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int ERRNO_ENOENT = 2;
    private static final int ERRNO_ENOTCONN = 57;
    private static final int ERRNO_ENOTCONN_LINUX = 107;
    private static final int ERRNO_ETIMEDOUT = 60;
    private static final int ERRNO_ETIMEDOUT_LINUX = 110;
    private static final int ERRNO_ECONNREFUSED = 61;
    private static final int ERRNO_ECONNREFUSED_LINUX = 111;
    private static final int ERRNO_EHOSTUNREACH = 65;
    private static final int ERRNO_EHOSTUNREACH_LINUX = 113;
    private static final int ERRNO_ECONNABORTED = 103;
    private static final int ERRNO_ECONNRESET = 104;
    private static final int ERRNO_WSAENOTCONN = 10057;
    private static final int ERRNO_WSAETIMEDOUT = 10060;
    private static final int ERRNO_WSAECONNREFUSED = 10061;
    private static final int ERRNO_WSAEHOSTUNREACH = 10065;
    private static final int ERRNO_EFSM = 156384763;
    private static final int POLL_TIMEOUT_IDLE_MS = 50;
    private static final int POLL_TIMEOUT_PENDING_MS = 10;

    private PerfMultiGatewayServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        Endpoints endpoints = resolveEndpoints(transport, "multi-gateway");
        int clients = Math.max(1, PerfMultiCommon.resolveClients(PATTERN));
        int connectTimeoutMs = PerfMultiCommon.resolveConnectReadyTimeoutMs();
        int warmupSeconds = Math.max(0, PerfMultiCommon.resolveWarmupSeconds());
        int durationSeconds = Math.max(1, PerfMultiCommon.resolveDurationSeconds());
        int settleSeconds = (Math.max(0, PerfMultiCommon.resolveSettleMs())
            + 999) / 1000;
        int payloadSize = Math.max(msgSize, Math.max(MIN_PAYLOAD_BYTES,
            STOP_TOKEN.length));

        try (Context context = new Context()) {
            PerfCommon.applyServerContextOptions(context);
            try (Registry registry = new Registry(context);
                 Receiver receiver = new Receiver(context,
                     SERVER_RECEIVER_ROUTING_ID);
                 Discovery discovery = new Discovery(context,
                     ServiceType.GATEWAY);
                 Gateway gateway = new Gateway(context, discovery,
                     SERVER_GATEWAY_ROUTING_ID);
                 Message replyMessage = new Message(payloadSize)) {

                registry.setEndpoints(endpoints.registryPub,
                    endpoints.registryRouter);
                registry.setHeartbeat(5000, 120000);
                registry.start();
                PerfCommon.sleepMillis(100);

                PerfMultiTls.configureReceiverTlsServerIfNeeded(receiver,
                    transport);
                applyReceiverOptions(receiver);
                receiver.bind(endpoints.serverEndpoint);
                connectReceiverRegistryWithRetry(receiver,
                    endpoints.registryRouter);
                registerReceiverWithRetry(receiver, SERVICE_NAME,
                    endpoints.serverEndpoint);
                if (!PerfCommon.waitUntil(
                    () -> receiver.registerResult(SERVICE_NAME).status() == 0,
                    connectTimeoutMs,
                    10)) {
                    throw new IllegalStateException(
                        "gateway_server_receiver_register_not_ready");
                }

                connectRegistryWithRetry(() ->
                    discovery.connectRegistry(endpoints.registryRouter));
                applyGatewayOptions(gateway);
                PerfMultiTls.configureGatewayTlsClientIfNeeded(gateway,
                    transport);

                System.out.println(
                    "READY," + endpoints.serverEndpoint + "|"
                        + endpoints.registryPub + "|"
                        + endpoints.registryRouter);

                PerfMultiMetricHeader.Header header =
                    new PerfMultiMetricHeader.Header();
                byte[] payload = new byte[payloadSize];
                String[] clientServices = buildClientServices(clients);
                PendingReply[] pendingReplies = createPendingReplies(clients,
                    payloadSize);
                int pendingReplyCount = 0;

                long recvCount = 0L;
                long benchStartNs = 0L;
                long benchEndNs = 0L;
                long activeHardStopNs = 0L;
                long firstPacketDeadlineNs = System.nanoTime()
                    + (long) Math.max(6, warmupSeconds + settleSeconds + 3)
                    * 1_000_000_000L;
                long lastActiveMessageNs = 0L;
                long idleBreakNs = Math.max(
                    PerfMultiCommon.resolveRcvTimeoutMs() * 2L, 1000L)
                    * 1_000_000L;
                PerfCommon.LatencyReservoir reservoir =
                    new PerfCommon.LatencyReservoir(
                        PerfMultiCommon.resolveLatencySampleCap());
                int pendingBackpressureThreshold = Math.max(1,
                    pendingReplies.length - 1);
                try (Poller poller = new Poller()) {
                    poller.addReceiver(receiver, PollEventType.POLLIN.getValue(),
                        "receiver");
                    poller.addGateway(gateway, 0, "gateway");

                    serverLoop:
                    while (true) {
                        long nowNs = System.nanoTime();
                        if (activeHardStopNs > 0 && nowNs >= activeHardStopNs) {
                            break;
                        }

                        pendingReplyCount = flushPendingReplies(gateway,
                            replyMessage, pendingReplies, pendingReplyCount);
                        poller.modifyReceiver(receiver,
                            pendingReplyCount < pendingBackpressureThreshold
                                ? PollEventType.POLLIN.getValue() : 0);
                        poller.modifyGateway(gateway, pendingReplyCount > 0
                            ? PollEventType.POLLOUT.getValue() : 0);

                        int readyCount = safePollCount(poller,
                            pendingReplyCount > 0 ? POLL_TIMEOUT_PENDING_MS
                                : POLL_TIMEOUT_IDLE_MS);
                        if (readyCount <= 0
                            || !hasReceiverReady(poller, readyCount)) {
                            long idleNowNs = System.nanoTime();
                            if (activeHardStopNs > 0
                                && idleNowNs >= activeHardStopNs) {
                                break;
                            }
                            if (lastActiveMessageNs > 0) {
                                if (idleNowNs - lastActiveMessageNs >= idleBreakNs) {
                                    break;
                                }
                            } else if (idleNowNs >= firstPacketDeadlineNs) {
                                break;
                            }
                            continue;
                        }

                        while (pendingReplyCount < pendingBackpressureThreshold) {
                            ReceivedRequest request = tryReceiveRequest(receiver,
                                payload);
                            if (request == null) {
                                break;
                            }
                            if (isStopToken(payload, request.payloadLength())) {
                                break serverLoop;
                            }

                            int clientIndex = parseClientIndex(
                                request.routingId(), clientServices.length);
                            if (clientIndex < 0) {
                                continue;
                            }

                            pendingReplyCount = enqueueOrSendReply(gateway,
                                replyMessage, pendingReplies,
                                pendingReplyCount, clientServices[clientIndex],
                                payload, request.payloadLength());

                            if (request.payloadLength() < HEADER_BYTES
                                || !PerfMultiMetricHeader.decodePayloadHeader(
                                    payload, header)
                                || header.msgSize != msgSize
                                || header.phase
                                    != PerfMultiMetricHeader.PHASE_ACTIVE) {
                                continue;
                            }

                            if (benchStartNs == 0L) {
                                benchStartNs = System.nanoTime();
                                activeHardStopNs = benchStartNs
                                    + (long) Math.max(2, durationSeconds + 1)
                                    * 1_000_000_000L;
                            }

                            benchEndNs = System.nanoTime();
                            lastActiveMessageNs = benchEndNs;
                            recvCount++;
                            long nowUs = PerfMultiMetricHeader.nowUs();
                            if (header.sentTsUs > 0 && nowUs >= header.sentTsUs) {
                                reservoir.add(nowUs - header.sentTsUs);
                            }
                        }
                    }
                }

                if (recvCount <= 0) {
                    System.err.println("ERROR,MULTI_GATEWAY,server,no_requests");
                    return 2;
                }

                double configuredSeconds = Math.max(1.0, durationSeconds);
                double throughput = recvCount / configuredSeconds;
                PerfCommon.Stats stats = reservoir.snapshot();
                PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                    stats.meanUs(), stats.p95Us(), stats.p99Us());
                return 0;
            }
        } catch (RuntimeException ex) {
            logFailure("server", ex);
            return 2;
        }
    }

    private static Endpoints resolveEndpoints(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return new Endpoints(
                transport + "://127.0.0.1:" + fixedPort,
                "tcp://127.0.0.1:"
                    + (fixedPort + REGISTRY_FIXED_PORT_OFFSET + 1),
                "tcp://127.0.0.1:"
                    + (fixedPort + REGISTRY_FIXED_PORT_OFFSET + 2)
            );
        }
        return new Endpoints(
            PerfCommon.endpointFor(transport, name),
            PerfCommon.endpointFor("tcp", name + "-registry-pub"),
            PerfCommon.endpointFor("tcp", name + "-registry-router")
        );
    }

    private static String[] buildClientServices(int clients) {
        String[] names = new String[Math.max(0, clients)];
        for (int i = 0; i < names.length; i++) {
            names[i] = CLIENT_SERVICE_PREFIX + i;
        }
        return names;
    }

    private static ReceivedRequest tryReceiveRequest(Receiver receiver,
                                                     byte[] payloadBuffer) {
        while (true) {
            try (Receiver.ReceiverMessages received =
                     receiver.recv(ReceiveFlag.DONTWAIT)) {
                Message[] parts = received.parts();
                if (parts.length == 0) {
                    return null;
                }
                int payloadLength = parts[parts.length - 1].copyTo(payloadBuffer);
                return new ReceivedRequest(
                    new String(received.routingId(), StandardCharsets.UTF_8),
                    payloadLength);
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isWouldBlock(errno)) {
                    return null;
                }
                throw ex;
            }
        }
    }

    private static int parseClientIndex(String routingId, int maxExclusive) {
        if (routingId == null || routingId.length() < 2
            || routingId.charAt(0) != 'c') {
            return -1;
        }
        int value = 0;
        for (int i = 1; i < routingId.length(); i++) {
            char ch = routingId.charAt(i);
            if (ch < '0' || ch > '9') {
                return -1;
            }
            value = value * 10 + (ch - '0');
            if (value < 0 || value >= maxExclusive) {
                return -1;
            }
        }
        return value;
    }

    private static boolean isStopToken(byte[] payload, int size) {
        if (size != STOP_TOKEN.length) {
            return false;
        }
        for (int i = 0; i < STOP_TOKEN.length; i++) {
            if (payload[i] != STOP_TOKEN[i]) {
                return false;
            }
        }
        return true;
    }

    private static int safeConnectionCount(Gateway gateway, String serviceName) {
        try {
            return gateway.connectionCount(serviceName);
        } catch (ZlinkException ex) {
            if (isInterrupted(ex.errno()) || isGatewaySendBlocked(ex.errno())) {
                return 0;
            }
            throw ex;
        }
    }

    private static PendingReply[] createPendingReplies(int clients,
                                                       int payloadSize) {
        int capacity = Math.max(64, Math.max(1, clients) + 1);
        PendingReply[] replies = new PendingReply[capacity];
        for (int i = 0; i < replies.length; i++) {
            replies[i] = new PendingReply(payloadSize);
        }
        return replies;
    }

    private static int enqueueOrSendReply(Gateway gateway,
                                          Message replyMessage,
                                          PendingReply[] pendingReplies,
                                          int pendingReplyCount,
                                          String serviceName,
                                          byte[] payload,
                                          int payloadLength) {
        if (trySendGatewayEcho(gateway, replyMessage, serviceName, payload,
                payloadLength)) {
            return pendingReplyCount;
        }
        if (pendingReplyCount >= pendingReplies.length) {
            throw new IllegalStateException("gateway_pending_reply_overflow");
        }
        pendingReplies[pendingReplyCount].set(serviceName, payload, payloadLength);
        return pendingReplyCount + 1;
    }

    private static int flushPendingReplies(Gateway gateway,
                                           Message replyMessage,
                                           PendingReply[] pendingReplies,
                                           int pendingReplyCount) {
        int index = 0;
        while (index < pendingReplyCount) {
            PendingReply reply = pendingReplies[index];
            if (!trySendGatewayEcho(gateway, replyMessage, reply.serviceName,
                    reply.payload, reply.payloadLength)) {
                index++;
                continue;
            }

            pendingReplyCount--;
            if (index != pendingReplyCount) {
                pendingReplies[index].copyFrom(pendingReplies[pendingReplyCount]);
            }
            pendingReplies[pendingReplyCount].clear();
        }
        return pendingReplyCount;
    }

    private static boolean trySendGatewayEcho(Gateway gateway,
                                              Message replyMessage,
                                              String serviceName,
                                              byte[] payload,
                                              int payloadLength) {
        if (!tryGatewayReady(gateway, serviceName)) {
            return false;
        }

        MemorySegment.copy(MemorySegment.ofArray(payload), 0,
            replyMessage.dataSegment(payloadLength), 0, payloadLength);
        while (true) {
            try {
                gateway.sendTo(serviceName, replyMessage, SendFlag.DONTWAIT);
                return true;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (isGatewaySendBlocked(errno)) {
                    return false;
                }
                throw ex;
            }
        }
    }

    private static boolean tryGatewayReady(Gateway gateway, String serviceName) {
        return safeConnectionCount(gateway, serviceName) > 0;
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

    private static boolean hasReceiverReady(Poller poller, int readyCount) {
        for (int i = 0; i < readyCount; i++) {
            if ((poller.readyRevents(i) & PollEventType.POLLIN.getValue()) != 0
                && "receiver".equals(poller.readyTag(i))) {
                return true;
            }
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
        receiver.connectRegistry(endpoint);
    }

    private static void registerReceiverWithRetry(Receiver receiver,
                                                  String serviceName,
                                                  String endpoint) {
        receiver.register(serviceName, endpoint, 1);
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

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isGatewaySendBlocked(int errno) {
        return isWouldBlock(errno)
            || errno == ERRNO_ENOENT
            || errno == ERRNO_ENOTCONN
            || errno == ERRNO_ENOTCONN_LINUX
            || errno == ERRNO_ETIMEDOUT
            || errno == ERRNO_ETIMEDOUT_LINUX
            || errno == ERRNO_ECONNREFUSED
            || errno == ERRNO_ECONNREFUSED_LINUX
            || errno == ERRNO_EHOSTUNREACH
            || errno == ERRNO_EHOSTUNREACH_LINUX
            || errno == ERRNO_ECONNABORTED
            || errno == ERRNO_ECONNRESET
            || errno == ERRNO_WSAENOTCONN
            || errno == ERRNO_WSAETIMEDOUT
            || errno == ERRNO_WSAECONNREFUSED
            || errno == ERRNO_WSAEHOSTUNREACH
            || errno == ERRNO_EFSM;
    }

    private static void logFailure(String role, RuntimeException ex) {
        String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
            : ex.getMessage();
        System.err.println("multi_gateway_" + role + "_error:" + message);
    }

    private static final class PendingReply {
        private String serviceName;
        private final byte[] payload;
        private int payloadLength;

        private PendingReply(int payloadCapacity) {
            this.serviceName = "";
            this.payload = new byte[Math.max(1, payloadCapacity)];
            this.payloadLength = 0;
        }

        private void set(String serviceName, byte[] source, int length) {
            this.serviceName = serviceName;
            this.payloadLength = Math.max(0, length);
            System.arraycopy(source, 0, payload, 0, this.payloadLength);
        }

        private void copyFrom(PendingReply other) {
            serviceName = other.serviceName;
            payloadLength = other.payloadLength;
            System.arraycopy(other.payload, 0, payload, 0, payloadLength);
        }

        private void clear() {
            serviceName = "";
            payloadLength = 0;
        }
    }

    private record Endpoints(String serverEndpoint, String registryPub,
                             String registryRouter) {
    }

    private record ReceivedRequest(String routingId, int payloadLength) {
    }
}
