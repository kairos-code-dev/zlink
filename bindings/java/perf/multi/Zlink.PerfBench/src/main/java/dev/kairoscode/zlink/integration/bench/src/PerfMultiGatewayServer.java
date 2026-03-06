/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
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
import dev.kairoscode.zlink.service.receiver.ReceiverSocketRole;
import dev.kairoscode.zlink.service.registry.Registry;
import java.nio.charset.StandardCharsets;

/**
 * MULTI_GATEWAY server benchmark.
 * Receiver(bind) consumes requests, Gateway sends echoes to client services.
 */
public final class PerfMultiGatewayServer {
    private static final String PATTERN = "MULTI_GATEWAY";
    private static final String SERVICE_NAME = "perf-server";
    private static final String CLIENT_SERVICE_PREFIX = "c";
    private static final String SERVER_GATEWAY_ROUTING_ID = "sg";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int ROUTING_ID_BUFFER_BYTES = 256;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;
    private static final long STARTUP_GRACE_NS = 5000L * NANOSECONDS_PER_MILLISECOND;
    private static final long IDLE_BREAK_NS = 1500L * NANOSECONDS_PER_MILLISECOND;

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

        try (Context context = new Context();
             Registry registry = new Registry(context);
             Receiver receiver = new Receiver(context);
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

            discovery.connectRegistry(endpoints.registryPub);
            for (int i = 0; i < clients; i++) {
                discovery.subscribe(CLIENT_SERVICE_PREFIX + i);
            }

            applyGatewayOptions(gateway);
            PerfMultiTls.configureGatewayTlsClientIfNeeded(gateway, transport);

            try (Socket router = receiver.routerSocket()) {
                applyRouterOptions(router);
                System.out.println(
                    "READY," + endpoints.serverEndpoint + "|"
                        + endpoints.registryPub + "|" + endpoints.registryRouter);

                byte[] routingId = new byte[ROUTING_ID_BUFFER_BYTES];
                byte[] payload = new byte[resolvePayloadSize(msgSize)];
                String[] clientServices = buildClientServices(clients);
                if (!waitForClientServices(gateway, clientServices,
                    connectTimeoutMs)) {
                    throw new IllegalStateException(
                        "gateway_client_services_not_ready");
                }
                long startNs = System.nanoTime();
                long lastActivityNs = startNs;
                boolean sawTraffic = false;

                while (true) {
                    int ridLen = receiveBlocking(router, routingId);
                    if (ridLen <= 0) {
                        long nowNs = System.nanoTime();
                        if (!sawTraffic) {
                            if (nowNs - startNs >= STARTUP_GRACE_NS) {
                                break;
                            }
                        } else if (nowNs - lastActivityNs >= IDLE_BREAK_NS) {
                            break;
                        }
                        continue;
                    }
                    if (getRcvMore(router) == 0) {
                        continue;
                    }

                    int n = receiveBlocking(router, payload);
                    if (n <= 0) {
                        continue;
                    }
                    if (getRcvMore(router) != 0) {
                        drainRemainingFramesBlocking(router, payload);
                        continue;
                    }

                    sawTraffic = true;
                    lastActivityNs = System.nanoTime();
                    if (isStopToken(payload, n)) {
                        break;
                    }

                    sendGatewayEcho(gateway, resolveClientService(routingId, ridLen,
                        clientServices), payload, n);

                    while (true) {
                        int drainedRidLen = receiveNonBlocking(router, routingId);
                        if (drainedRidLen <= 0) {
                            break;
                        }
                        if (getRcvMore(router) == 0) {
                            continue;
                        }

                        int drainedPayloadLen = receiveNonBlocking(router, payload);
                        if (drainedPayloadLen <= 0) {
                            throw new IllegalStateException("gateway_partial_message");
                        }
                        if (getRcvMore(router) != 0) {
                            drainRemainingFramesBlocking(router, payload);
                            continue;
                        }
                        if (isStopToken(payload, drainedPayloadLen)) {
                            return 0;
                        }

                        sendGatewayEcho(gateway,
                            resolveClientService(routingId, drainedRidLen,
                                clientServices),
                            payload, drainedPayloadLen);
                    }
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
        if (index >= 0) {
            return clientServices[index];
        }
        return new String(routingId, 0, ridLen, StandardCharsets.US_ASCII);
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

    private static boolean waitForClientServices(Gateway gateway,
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
                if (gateway.connectionCount(clientServices[i]) > 0) {
                    ready[i] = true;
                    readyCount++;
                }
            }
            if (readyCount < clientServices.length) {
                PerfCommon.sleepMillis(5);
            }
        }
        return readyCount > 0;
    }

    private static void applyReceiverOptions(Receiver receiver) {
        receiver.setOption(ReceiverSocketRole.ROUTER, SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        receiver.setOption(ReceiverSocketRole.ROUTER, SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        receiver.setOption(ReceiverSocketRole.DEALER, SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        receiver.setOption(ReceiverSocketRole.DEALER, SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        receiver.setOption(ReceiverSocketRole.ROUTER, SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        receiver.setOption(ReceiverSocketRole.ROUTER, SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        receiver.setOption(ReceiverSocketRole.DEALER, SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        receiver.setOption(ReceiverSocketRole.DEALER, SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        receiver.setOption(ReceiverSocketRole.ROUTER, SocketOptions.LINGER, 0);
        receiver.setOption(ReceiverSocketRole.DEALER, SocketOptions.LINGER, 0);
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

    private static void sendGatewayEcho(Gateway gateway,
                                        String clientService,
                                        byte[] payload,
                                        int payloadLen) {
        try (Message echo = Message.fromBytes(payload, 0, payloadLen)) {
            gateway.sendTo(clientService, echo, SendFlag.NONE);
        }
    }

    private static void drainRemainingFramesBlocking(Socket socket,
                                                     byte[] scratch) {
        while (getRcvMore(socket) != 0) {
            int drained = receiveBlocking(socket, scratch);
            if (drained <= 0) {
                break;
            }
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

    private static int receiveBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.NONE);
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
