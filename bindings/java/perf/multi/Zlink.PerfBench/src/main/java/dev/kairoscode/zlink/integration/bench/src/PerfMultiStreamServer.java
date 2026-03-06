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
import java.util.HashMap;
import java.util.Map;

/**
 * MULTI_STREAM server benchmark.
 * STREAM(bind) echoes payload and exits on stop token.
 */
public final class PerfMultiStreamServer {
    private static final String PATTERN = "MULTI_STREAM";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MIN_PAYLOAD_BYTES = 32;
    private static final int ROUTING_ID_BUFFER_BYTES = 1024;
    private static final int STREAM_PAYLOAD_BUFFER_BYTES = 16 * 1024 * 1024;
    private static final int MAX_STREAM_FRAME_BYTES = 16 * 1024 * 1024;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;

    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private PerfMultiStreamServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        String endpoint = resolveServerEndpoint(transport, "multi-stream");

        int rcvTimeoutMs = Math.max(100,
            Math.min(500, PerfMultiCommon.resolveRcvTimeoutMs()));
        long idleBreakNs = 2000L * NANOSECONDS_PER_MILLISECOND;
        long startupGraceNs = 12000L * NANOSECONDS_PER_MILLISECOND;

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.STREAM)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.setOption(SocketOptions.RCVTIMEO, rcvTimeoutMs);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);

            byte[] routingId = new byte[ROUTING_ID_BUFFER_BYTES];
            byte[] payload = new byte[resolvePayloadSize(msgSize)];
            Map<Integer, Len32StopTokenParser> stopParsers = new HashMap<>();

            long lastActivityNs = System.nanoTime();
            long startNs = lastActivityNs;
            boolean sawTraffic = false;

            while (true) {
                int ridLen = receiveBlockingFrame(server, routingId);
                if (ridLen <= 0) {
                    long nowNs = System.nanoTime();
                    if (!sawTraffic) {
                        if (nowNs - startNs >= startupGraceNs) {
                            break;
                        }
                    } else if (nowNs - lastActivityNs >= idleBreakNs) {
                        break;
                    }
                    continue;
                }
                if (getRcvMore(server) == 0) {
                    // Orphan frame (no payload part): ignore and keep frame alignment.
                    continue;
                }

                int n = receiveBlockingFrame(server, payload);
                if (n <= 0) {
                    continue;
                }
                if (getRcvMore(server) != 0) {
                    while (getRcvMore(server) != 0) {
                        if (receiveBlockingFrame(server, payload) <= 0) {
                            break;
                        }
                    }
                    continue;
                }
                if (isStreamControl(payload, n)) {
                    continue;
                }

                int key = routingKey(routingId, ridLen);
                Len32StopTokenParser parser = stopParsers.computeIfAbsent(key,
                    ignored -> new Len32StopTokenParser());
                if (isStopToken(payload, n) || parser.consume(payload, n)) {
                    break;
                }

                sawTraffic = true;
                lastActivityNs = System.nanoTime();
                sendBlocking(server, routingId, 0, ridLen, SendFlag.SNDMORE);
                sendBlocking(server, payload, 0, n, SendFlag.NONE);

                while (true) {
                    int drainedRidLen = receiveNonBlockingFrame(server, routingId);
                    if (drainedRidLen <= 0) {
                        break;
                    }
                    if (getRcvMore(server) == 0) {
                        continue;
                    }
                    int drainedPayloadLen = receiveNonBlockingFrame(server, payload);
                    if (drainedPayloadLen <= 0) {
                        throw new IllegalStateException("stream_partial_message");
                    }
                    if (getRcvMore(server) != 0) {
                        while (getRcvMore(server) != 0) {
                            if (receiveBlockingFrame(server, payload) <= 0) {
                                break;
                            }
                        }
                        continue;
                    }
                    if (isStreamControl(payload, drainedPayloadLen)) {
                        continue;
                    }
                    int drainedKey = routingKey(routingId, drainedRidLen);
                    Len32StopTokenParser drainedParser = stopParsers.computeIfAbsent(
                        drainedKey, ignored -> new Len32StopTokenParser());
                    if (isStopToken(payload, drainedPayloadLen)
                        || drainedParser.consume(payload, drainedPayloadLen)) {
                        return 0;
                    }
                    sendBlocking(server, routingId, 0, drainedRidLen,
                        SendFlag.SNDMORE);
                    sendBlocking(server, payload, 0, drainedPayloadLen,
                        SendFlag.NONE);
                }
            }
            return 0;
        } catch (RuntimeException ignored) {
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
            if (size == STOP_TOKEN.length + 4) {
                int len = ((payload[0] & 0xFF) << 24)
                    | ((payload[1] & 0xFF) << 16)
                    | ((payload[2] & 0xFF) << 8)
                    | (payload[3] & 0xFF);
                if (len == STOP_TOKEN.length) {
                    for (int i = 0; i < STOP_TOKEN.length; i++) {
                        if (payload[4 + i] != STOP_TOKEN[i]) {
                            return false;
                        }
                    }
                    return true;
                }
            }
            return false;
        }

        for (int i = 0; i < STOP_TOKEN.length; i++) {
            if (payload[i] != STOP_TOKEN[i]) {
                return false;
            }
        }
        return true;
    }

    private static boolean isStreamControl(byte[] payload, int size) {
        return size == 0 || (size == 1 && (payload[0] == 0 || payload[0] == 1));
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

    private static int receiveBlockingFrame(Socket socket, byte[] buffer) {
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

    private static int receiveNonBlockingFrame(Socket socket, byte[] buffer) {
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

    private static int getRcvMore(Socket socket) {
        Integer value = socket.getOption(SocketOptions.RCVMORE);
        return value == null ? 0 : value;
    }

    private static int routingKey(byte[] routingId, int ridLen) {
        int hash = 1;
        int length = Math.max(0, Math.min(ridLen, routingId.length));
        for (int i = 0; i < length; i++) {
            hash = 31 * hash + (routingId[i] & 0xFF);
        }
        return hash;
    }

    private static int resolvePayloadSize(int msgSize) {
        return Math.max(STREAM_PAYLOAD_BUFFER_BYTES,
            Math.max(msgSize, Math.max(MIN_PAYLOAD_BYTES, STOP_TOKEN.length)));
    }

    private static final class Len32StopTokenParser {
        private byte[] buffer = new byte[2048];
        private int start = 0;
        private int end = 0;

        boolean consume(byte[] chunk, int chunkLen) {
            if (chunkLen <= 0) {
                return false;
            }
            ensureCapacity(chunkLen);
            System.arraycopy(chunk, 0, buffer, end, chunkLen);
            end += chunkLen;

            boolean found = false;
            while ((end - start) >= 4) {
                int bodyLen = ((buffer[start] & 0xFF) << 24)
                    | ((buffer[start + 1] & 0xFF) << 16)
                    | ((buffer[start + 2] & 0xFF) << 8)
                    | (buffer[start + 3] & 0xFF);
                if (bodyLen < 0 || bodyLen > MAX_STREAM_FRAME_BYTES) {
                    start = 0;
                    end = 0;
                    return false;
                }

                int frameLen = 4 + bodyLen;
                if ((end - start) < frameLen) {
                    break;
                }

                if (bodyLen == STOP_TOKEN.length) {
                    boolean token = true;
                    for (int i = 0; i < STOP_TOKEN.length; i++) {
                        if (buffer[start + 4 + i] != STOP_TOKEN[i]) {
                            token = false;
                            break;
                        }
                    }
                    if (token) {
                        found = true;
                    }
                }
                start += frameLen;
            }

            compact();
            return found;
        }

        private void ensureCapacity(int incoming) {
            int needed = end + incoming;
            if (needed <= buffer.length) {
                return;
            }

            compact();
            needed = end + incoming;
            if (needed <= buffer.length) {
                return;
            }

            int next = buffer.length;
            while (next < needed) {
                next *= 2;
            }

            byte[] grown = new byte[next];
            int remain = end - start;
            if (remain > 0) {
                System.arraycopy(buffer, start, grown, 0, remain);
            }
            buffer = grown;
            start = 0;
            end = remain;
        }

        private void compact() {
            if (start <= 0) {
                return;
            }
            if (start >= end) {
                start = 0;
                end = 0;
                return;
            }

            int remain = end - start;
            System.arraycopy(buffer, start, buffer, 0, remain);
            start = 0;
            end = remain;
        }
    }
}
