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
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int HEADER_BYTES = 32;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

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

        try (Context context = new Context();
             Registry registry = new Registry(context);
             Receiver receiver = new Receiver(context, SERVER_RECEIVER_ROUTING_ID);
             Discovery discovery = new Discovery(context, ServiceType.GATEWAY);
             Gateway gateway = new Gateway(context, discovery,
                 SERVER_GATEWAY_ROUTING_ID);
             Message replyMessage = new Message(payloadSize)) {
            PerfCommon.applyServerContextOptions(context);

            registry.setEndpoints(endpoints.registryPub, endpoints.registryRouter);
            registry.setHeartbeat(5000, 120000);
            registry.start();
            PerfCommon.sleepMillis(100);

            PerfMultiTls.configureReceiverTlsServerIfNeeded(receiver, transport);
            applyReceiverOptions(receiver);
            receiver.bind(endpoints.serverEndpoint);
            connectReceiverRegistryWithRetry(receiver, endpoints.registryRouter);
            receiver.register(SERVICE_NAME, endpoints.serverEndpoint, 1);
            if (!PerfCommon.waitUntil(
                () -> receiver.registerResult(SERVICE_NAME).status() == 0,
                connectTimeoutMs,
                10)) {
                throw new IllegalStateException(
                    "gateway_server_receiver_register_not_ready");
            }

            connectRegistryWithRetry(() ->
                discovery.connectRegistry(endpoints.registryRouter));
            for (int i = 0; i < clients; i++) {
                discovery.subscribe(CLIENT_SERVICE_PREFIX + i);
            }

            applyGatewayOptions(gateway);
            PerfMultiTls.configureGatewayTlsClientIfNeeded(gateway, transport);

            System.out.println(
                "READY," + endpoints.serverEndpoint + "|" + endpoints.registryPub
                    + "|" + endpoints.registryRouter);

            String[] clientServices = buildClientServices(clients);
            if (!waitForClientServices(discovery, gateway, clientServices,
                connectTimeoutMs)) {
                throw new IllegalStateException("gateway_client_services_not_ready");
            }

            PerfMultiMetricHeader.Header header = new PerfMultiMetricHeader.Header();
            byte[] payload = new byte[payloadSize];
            MemorySegment payloadSegment = MemorySegment.ofArray(payload);

            long recvCount = 0L;
            long benchStartNs = 0L;
            long benchEndNs = 0L;
            long activeHardStopNs = 0L;
            long firstPacketDeadlineNs = System.nanoTime()
                + (long) Math.max(6, warmupSeconds + settleSeconds + 3)
                * 1_000_000_000L;
            long lastActiveMessageNs = 0L;
            long idleBreakNs = Math.max(
                PerfMultiCommon.resolveRcvTimeoutMs() * 2L, 1000L) * 1_000_000L;
            PerfCommon.LatencyReservoir reservoir =
                new PerfCommon.LatencyReservoir(
                    PerfMultiCommon.resolveLatencySampleCap());

            while (true) {
                long nowNs = System.nanoTime();
                if (activeHardStopNs > 0 && nowNs >= activeHardStopNs) {
                    break;
                }

                ReceivedRequest request = tryReceiveRequest(receiver, payload);
                if (request == null) {
                    if (activeHardStopNs > 0 && nowNs >= activeHardStopNs) {
                        break;
                    }
                    if (lastActiveMessageNs > 0) {
                        if (nowNs - lastActiveMessageNs >= idleBreakNs) {
                            break;
                        }
                    } else if (nowNs >= firstPacketDeadlineNs) {
                        break;
                    }
                    PerfCommon.sleepMillis(1);
                    continue;
                }

                if (isStopToken(payload, request.payloadLength())) {
                    break;
                }

                int clientIndex = parseClientIndex(request.routingId(),
                    clientServices.length);
                if (clientIndex < 0) {
                    continue;
                }

                MemorySegment.copy(payloadSegment, 0, replyMessage.dataSegment(), 0,
                    request.payloadLength());
                sendReplyWithRetry(gateway, clientServices[clientIndex],
                    replyMessage);

                if (request.payloadLength() < HEADER_BYTES
                    || !PerfMultiMetricHeader.decodePayloadHeader(payload, header)
                    || header.msgSize != msgSize
                    || header.phase != PerfMultiMetricHeader.PHASE_ACTIVE) {
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

            if (recvCount <= 0) {
                System.err.println("ERROR,MULTI_GATEWAY,server,no_requests");
                return 2;
            }

            double elapsedSec = Math.max(1e-9,
                (benchEndNs - benchStartNs) / 1_000_000_000.0);
            double throughput = recvCount / elapsedSec;
            PerfCommon.Stats stats = reservoir.snapshot();
            PerfCommon.printResult(PATTERN, transport, msgSize, throughput,
                stats.meanUs(), stats.p95Us(), stats.p99Us());
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

    private static boolean waitForClientServices(Discovery discovery,
                                                 Gateway gateway,
                                                 String[] clientServices,
                                                 int timeoutMs) {
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
                    && safeConnectionCount(gateway, clientServices[i]) > 0) {
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

    private static void sendReplyWithRetry(Gateway gateway,
                                           String serviceName,
                                           Message payload) {
        long deadlineNs = System.nanoTime()
            + (long) Math.max(PerfMultiCommon.resolveSndTimeoutMs(), 200)
            * 1_000_000L;
        RuntimeException last = null;
        while (System.nanoTime() < deadlineNs) {
            try {
                gateway.sendTo(serviceName, payload, SendFlag.DONTWAIT);
                return;
            } catch (ZlinkException ex) {
                int errno = ex.errno();
                if (isInterrupted(errno)) {
                    continue;
                }
                if (!isWouldBlock(errno)) {
                    throw ex;
                }
                last = ex;
            }
            PerfCommon.sleepMillis(1);
        }

        if (last != null) {
            throw last;
        }
        throw new IllegalStateException("gateway_reply_timeout");
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
            if (isInterrupted(ex.errno()) || isWouldBlock(ex.errno())) {
                return 0;
            }
            throw ex;
        }
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

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static void logFailure(String role, RuntimeException ex) {
        String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
            : ex.getMessage();
        System.err.println("multi_gateway_" + role + "_error:" + message);
    }

    private record Endpoints(String serverEndpoint, String registryPub,
                             String registryRouter) {
    }

    private record ReceivedRequest(String routingId, int payloadLength) {
    }
}
