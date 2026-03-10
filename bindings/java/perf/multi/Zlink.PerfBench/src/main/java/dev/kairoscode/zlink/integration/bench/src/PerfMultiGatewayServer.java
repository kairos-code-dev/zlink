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
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.gateway.Gateway;
import dev.kairoscode.zlink.service.receiver.Receiver;
import dev.kairoscode.zlink.service.registry.Registry;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;

/**
 * MULTI_GATEWAY server benchmark.
 * Receiver(bind) consumes requests, Gateway sends echoes to client services.
 */
public final class PerfMultiGatewayServer {
    private static final String PATTERN = "MULTI_GATEWAY";
    private static final String SERVICE_NAME = "perf-server";
    private static final String SERVER_RECEIVER_ROUTING_ID = "perf-server-rx";
    private static final String CLIENT_SERVICE_PREFIX = "c";
    private static final String SERVER_GATEWAY_ROUTING_ID = "sg";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int ROUTING_ID_BUFFER_BYTES = 256;
    private static final int RECEIVER_TAG = 1;
    private static final int GATEWAY_TAG = 2;
    private static final int DEADLINE_GRACE_SECONDS = 5;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final class RelayCounters {
        long requestCount;
        long replyImmediateCount;
        long replyFlushedCount;
        long replyBlockedCount;
    }

    private static final class PendingReply {
        final byte[] payload;
        final MemorySegment payloadSegment;
        final Message message;
        int payloadLen;
        boolean pending;

        PendingReply(int payloadSize) {
            this.payload = new byte[payloadSize];
            this.payloadSegment = MemorySegment.ofArray(this.payload);
            this.message = new Message(payloadSize);
            this.payloadLen = 0;
            this.pending = false;
        }

        void prepare(byte[] source, int length) {
            System.arraycopy(source, 0, payload, 0, length);
            MemorySegment.copy(payloadSegment, 0, message.dataSegment(), 0,
                length);
            payloadLen = length;
            pending = true;
        }

        void clear() {
            payloadLen = 0;
            pending = false;
        }

        void close() {
            message.close();
        }
    }

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
        int pollTimeoutMs = Math.max(1,
            PerfMultiCommon.resolveClientPollTimeoutMs());
        int payloadSize = resolvePayloadSize(msgSize);

        try (Context context = new Context();
             Registry registry = new Registry(context);
             Receiver receiver = new Receiver(context, SERVER_RECEIVER_ROUTING_ID);
             Discovery discovery = new Discovery(context, ServiceType.GATEWAY);
             Gateway gateway = new Gateway(context, discovery,
                 SERVER_GATEWAY_ROUTING_ID)) {
            PerfCommon.applyServerContextOptions(context);

            registry.setEndpoints(endpoints.registryPub, endpoints.registryRouter);
            registry.setHeartbeat(5000, 120000);
            registry.start();

            PerfMultiTls.configureReceiverTlsServerIfNeeded(receiver, transport);
            applyReceiverOptions(receiver);
            receiver.bind(endpoints.serverEndpoint);
            receiver.connectRegistry(endpoints.registryRouter);
            receiver.register(SERVICE_NAME, endpoints.serverEndpoint, 1);
            if (!PerfCommon.waitUntil(
                () -> receiver.registerResult(SERVICE_NAME).status() == 0,
                connectTimeoutMs,
                10)) {
                throw new IllegalStateException(
                    "gateway_server_receiver_register_not_ready");
            }

            discovery.connectRegistry(endpoints.registryPub);
            for (int i = 0; i < clients; i++) {
                discovery.subscribe(CLIENT_SERVICE_PREFIX + i);
            }

            applyGatewayOptions(gateway);
            PerfMultiTls.configureGatewayTlsClientIfNeeded(gateway, transport);

            try (Socket router = receiver.routerSocket();
                 Poller poller = new Poller()) {
                applyRouterOptions(router);
                System.out.println(
                    "READY," + endpoints.serverEndpoint + "|"
                        + endpoints.registryPub + "|" + endpoints.registryRouter);

                byte[] routingId = new byte[ROUTING_ID_BUFFER_BYTES];
                byte[] payload = new byte[payloadSize];
                String[] clientServices = buildClientServices(clients);
                PendingReply[] pendingReplies = new PendingReply[clients];
                for (int i = 0; i < pendingReplies.length; i++) {
                    pendingReplies[i] = new PendingReply(payloadSize);
                }
                if (!waitForClientServices(discovery, gateway, clientServices,
                    connectTimeoutMs)) {
                    throw new IllegalStateException(
                        "gateway_client_services_not_ready");
                }

                poller.addReceiver(receiver, PollEventType.POLLIN.getValue(),
                    Integer.valueOf(RECEIVER_TAG));
                poller.addGateway(gateway, 0,
                    Integer.valueOf(GATEWAY_TAG));

                long deadlineNs = System.nanoTime()
                    + (long) (warmupSeconds + settleSeconds + durationSeconds
                    + DEADLINE_GRACE_SECONDS) * 1_000_000_000L;
                boolean stopRequested = false;
                RelayCounters counters = new RelayCounters();

                try {
                    while (!stopRequested && System.nanoTime() < deadlineNs) {
                        if (!flushPendingReplies(gateway, clientServices,
                            pendingReplies, counters)) {
                            throw new IllegalStateException(
                                "gateway_pending_flush_failed");
                        }
                        setGatewayPollout(poller, gateway,
                            hasPendingReplies(pendingReplies));

                        int eventCount = poller.pollCount(computeWaitMs(deadlineNs,
                            pollTimeoutMs));
                        for (int i = 0; i < eventCount && !stopRequested; i++) {
                            Object tag = poller.readyTag(i);
                            if (!(tag instanceof Integer)) {
                                continue;
                            }
                            int eventTag = (Integer) tag;
                            short revents = poller.readyRevents(i);
                            if (eventTag == GATEWAY_TAG) {
                                if ((revents
                                    & PollEventType.POLLOUT.getValue()) != 0
                                    && !flushPendingReplies(gateway,
                                    clientServices, pendingReplies,
                                    counters)) {
                                    throw new IllegalStateException(
                                        "gateway_pending_flush_failed");
                                }
                                continue;
                            }
                            if (eventTag != RECEIVER_TAG
                                || (revents & PollEventType.POLLIN.getValue())
                                == 0) {
                                continue;
                            }
                            stopRequested = drainReceiverRequests(router, gateway,
                                clientServices, pendingReplies, routingId,
                                payload, counters);
                        }
                    }
                } finally {
                    closePendingReplies(pendingReplies);
                }

                if (counters.requestCount == 0) {
                    System.err.println("ERROR,MULTI_GATEWAY,server,no_requests,"
                        + "client_services=" + discovery.receiverCount(
                        clientServices[0]) + ",gateway_connections="
                        + gateway.connectionCount(clientServices[0]));
                    return 2;
                }
            }

            return 0;
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
                "tcp://127.0.0.1:" + (fixedPort + 1),
                "tcp://127.0.0.1:" + (fixedPort + 2)
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

    private static String resolveClientService(byte[] routingId,
                                               int ridLen,
                                               String[] clientServices) {
        int index = parseClientIndex(routingId, ridLen, clientServices.length);
        if (index < 0) {
            throw new IllegalStateException("gateway_unexpected_client_service");
        }
        return clientServices[index];
    }

    private static int parseClientIndex(byte[] routingId,
                                        int ridLen,
                                        int maxExclusive) {
        if (ridLen < 2 || routingId[0] != (byte) 'c') {
            return -1;
        }
        int value = 0;
        for (int i = 1; i < ridLen; i++) {
            byte b = routingId[i];
            if (b < (byte) '0' || b > (byte) '9') {
                return -1;
            }
            value = value * 10 + (b - (byte) '0');
            if (value < 0 || value >= maxExclusive) {
                return -1;
            }
        }
        return value;
    }

    private static boolean waitForClientServices(Discovery discovery,
                                                 Gateway gateway,
                                                 String[] clientServices,
                                                 int timeoutMs) {
        if (clientServices.length == 0) {
            return false;
        }

        long deadlineNs = System.nanoTime()
            + (long) Math.max(1, timeoutMs) * 1_000_000L;
        boolean[] ready = new boolean[clientServices.length];
        int readyCount = 0;

        while (System.nanoTime() < deadlineNs && readyCount < clientServices.length) {
            for (int i = 0; i < clientServices.length; i++) {
                if (ready[i]) {
                    continue;
                }
                if (discovery.receiverCount(clientServices[i]) > 0
                    && gateway.connectionCount(clientServices[i]) > 0) {
                    ready[i] = true;
                    readyCount++;
                }
            }
            if (readyCount < clientServices.length) {
                PerfCommon.sleepMillis(5);
            }
        }
        return readyCount == clientServices.length;
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

    private static void applyRouterOptions(Socket socket) {
        socket.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        socket.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        socket.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        socket.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        socket.setOption(SocketOptions.LINGER, 0);
    }

    private static boolean drainReceiverRequests(Socket router,
                                                 Gateway gateway,
                                                 String[] clientServices,
                                                 PendingReply[] pendingReplies,
                                                 byte[] routingId,
                                                 byte[] payload,
                                                 RelayCounters counters) {
        while (true) {
            int routingIdLen = receiveNonBlocking(router, routingId);
            if (routingIdLen <= 0) {
                return false;
            }
            if (getRcvMore(router) == 0) {
                throw new IllegalStateException("gateway_missing_payload");
            }

            int payloadLen = receiveNonBlocking(router, payload);
            if (payloadLen <= 0) {
                throw new IllegalStateException("gateway_partial_message");
            }
            while (getRcvMore(router) != 0) {
                int drained = receiveNonBlocking(router, payload);
                if (drained <= 0) {
                    throw new IllegalStateException("gateway_partial_message");
                }
                payloadLen = drained;
            }

            if (isStopToken(payload, payloadLen)) {
                return true;
            }

            int clientIndex = parseClientIndex(routingId, routingIdLen,
                clientServices.length);
            if (clientIndex < 0) {
                throw new IllegalStateException("gateway_unexpected_client_service");
            }
            counters.requestCount++;
            tryStartReply(gateway, clientServices[clientIndex],
                pendingReplies[clientIndex], payload, payloadLen, counters);
        }
    }

    private static void tryStartReply(Gateway gateway,
                                      String clientService,
                                      PendingReply pendingReply,
                                      byte[] payload,
                                      int payloadLen,
                                      RelayCounters counters) {
        if (pendingReply.pending) {
            throw new IllegalStateException("gateway_pending_overflow");
        }
        pendingReply.prepare(payload, payloadLen);
        if (tryFlushPendingReply(gateway, clientService, pendingReply)) {
            counters.replyImmediateCount++;
        } else {
            counters.replyBlockedCount++;
        }
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

    private static boolean flushPendingReplies(Gateway gateway,
                                               String[] clientServices,
                                               PendingReply[] pendingReplies,
                                               RelayCounters counters) {
        for (int i = 0; i < pendingReplies.length; i++) {
            if (!pendingReplies[i].pending) {
                continue;
            }
            if (!tryFlushPendingReply(gateway, clientServices[i],
                pendingReplies[i])) {
                return false;
            }
            counters.replyFlushedCount++;
        }
        return true;
    }

    private static boolean tryFlushPendingReply(Gateway gateway,
                                                String clientService,
                                                PendingReply pendingReply) {
        if (!pendingReply.pending) {
            return true;
        }
        while (true) {
            try {
                gateway.sendTo(clientService, pendingReply.message,
                    SendFlag.DONTWAIT);
                pendingReply.clear();
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

    private static void setGatewayPollout(Poller poller,
                                          Gateway gateway,
                                          boolean enabled) {
        poller.modifyGateway(gateway,
            enabled ? PollEventType.POLLOUT.getValue() : 0);
    }

    private static boolean hasPendingReplies(PendingReply[] pendingReplies) {
        for (PendingReply pendingReply : pendingReplies) {
            if (pendingReply.pending) {
                return true;
            }
        }
        return false;
    }

    private static void closePendingReplies(PendingReply[] pendingReplies) {
        for (PendingReply pendingReply : pendingReplies) {
            if (pendingReply != null) {
                pendingReply.close();
            }
        }
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

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static int computeWaitMs(long deadlineNs, int pollTimeoutMs) {
        int waitMs = pollTimeoutMs > 0 ? pollTimeoutMs : 100;
        long remainingMs = (deadlineNs - System.nanoTime()) / 1_000_000L;
        if (remainingMs < waitMs) {
            waitMs = (int) remainingMs;
        }
        return Math.max(1, waitMs);
    }

    private static int resolvePayloadSize(int msgSize) {
        return Math.max(msgSize, Math.max(MIN_PAYLOAD_BYTES, STOP_TOKEN.length));
    }

    private static void logFailure(String role, RuntimeException ex) {
        String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
            : ex.getMessage();
        System.err.println("multi_gateway_" + role + "_error:" + message);
    }

    private record Endpoints(String serverEndpoint, String registryPub,
                             String registryRouter) {
    }
}
