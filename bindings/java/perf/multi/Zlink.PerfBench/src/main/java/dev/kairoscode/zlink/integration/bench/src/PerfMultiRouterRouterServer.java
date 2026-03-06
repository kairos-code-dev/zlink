/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import java.nio.charset.StandardCharsets;

/**
 * MULTI_ROUTER_ROUTER server benchmark.
 * ROUTER(bind) echoes payload back to sender routing-id.
 */
public final class PerfMultiRouterRouterServer {
    private static final String PATTERN = "MULTI_ROUTER_ROUTER";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int ROUTING_ID_BUFFER_BYTES = 256;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfMultiRouterRouterServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        String endpoint = resolveServerEndpoint(transport, "multi-router-router");

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.ROUTER)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);

            byte[] routingId = new byte[ROUTING_ID_BUFFER_BYTES];
            byte[] payload = new byte[resolvePayloadSize(msgSize)];

            while (true) {
                int ridLen = receiveBlocking(server, routingId);
                if (ridLen <= 0) {
                    continue;
                }
                if (getRcvMore(server) == 0) {
                    continue;
                }

                int n = receiveBlocking(server, payload);
                if (n <= 0) {
                    continue;
                }
                if (getRcvMore(server) != 0) {
                    drainRemainingFramesBlocking(server, payload);
                    continue;
                }
                if (isStopToken(payload, n)) {
                    break;
                }

                // ROUTER echo preserves sender routing-id as first frame.
                sendBlocking(server, routingId, 0, ridLen, SendFlag.SNDMORE);
                sendBlocking(server, payload, 0, n, SendFlag.NONE);

                while (true) {
                    int drainedRidLen = receiveNonBlocking(server, routingId);
                    if (drainedRidLen <= 0) {
                        break;
                    }
                    if (getRcvMore(server) == 0) {
                        continue;
                    }

                    int drainedPayloadLen = receiveBlocking(server, payload);
                    if (drainedPayloadLen <= 0) {
                        break;
                    }
                    if (getRcvMore(server) != 0) {
                        drainRemainingFramesBlocking(server, payload);
                        continue;
                    }
                    if (isStopToken(payload, drainedPayloadLen)) {
                        return 0;
                    }
                    sendBlocking(server, routingId, 0, drainedRidLen,
                        SendFlag.SNDMORE);
                    sendBlocking(server, payload, 0, drainedPayloadLen,
                        SendFlag.NONE);
                }
            }
            return 0;
        } catch (RuntimeException ex) {
            String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
                : ex.getMessage();
            System.err.println("multi_router_router_server_error:" + message);
            return 2;
        }
    }

    private static void applySocketOptions(Socket socket) {
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

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
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

    private static int receiveNonBlocking(Socket socket, byte[] buffer) {
        while (true) {
            try {
                return socket.recv(buffer, 0, buffer.length, ReceiveFlag.DONTWAIT);
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

    private static void sendBlocking(Socket socket, byte[] payload, int offset,
                                     int length, SendFlag flags) {
        SendFlag op = flags == null ? SendFlag.NONE : flags;
        int written = socket.send(payload, offset, length, op);
        if (written <= 0) {
            throw new IllegalStateException("send_failed");
        }
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static int getRcvMore(Socket socket) {
        Integer value = socket.getOption(SocketOptions.RCVMORE);
        return value == null ? 0 : value;
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

    private static int resolvePayloadSize(int msgSize) {
        return Math.max(msgSize, Math.max(MIN_PAYLOAD_BYTES, STOP_TOKEN.length));
    }
}
