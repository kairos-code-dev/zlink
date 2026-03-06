/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import java.nio.charset.StandardCharsets;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * MULTI_STREAM_CALLBACK server benchmark.
 * STREAM(bind) with attachStreamRaw(callback), echoes payload and exits on stop token.
 */
public final class PerfMultiStreamCallbackServer {
    private static final String PATTERN = "MULTI_STREAM_CALLBACK";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final int MAX_STREAM_FRAME_BYTES = 16 * 1024 * 1024;
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;

    private PerfMultiStreamCallbackServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        String endpoint = resolveServerEndpoint(transport,
            "multi-stream-callback");

        int warmupSeconds = PerfMultiCommon.resolveWarmupSeconds();
        int durationSeconds = PerfMultiCommon.resolveDurationSeconds();
        int settleMs = PerfMultiCommon.resolveSettleMs();
        int rcvTimeoutMs = Math.max(100,
            Math.min(500, PerfMultiCommon.resolveRcvTimeoutMs()));

        long firstPacketDeadlineNs = System.nanoTime()
            + (long) Math.max(8,
                warmupSeconds + durationSeconds + ((settleMs + 999) / 1000) + 5)
            * 1_000_000_000L;
        long idleBreakNs =
            (long) Math.max(rcvTimeoutMs * 2L, 1000L) * NANOSECONDS_PER_MILLISECOND;

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.STREAM)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.setOption(SocketOptions.RCVTIMEO, rcvTimeoutMs);

            AtomicBoolean stopRequested = new AtomicBoolean(false);
            AtomicBoolean callbackFailed = new AtomicBoolean(false);
            AtomicLong payloadSeen = new AtomicLong(0L);
            AtomicLong lastActivityNs = new AtomicLong(System.nanoTime());
            Map<Long, Len32StopTokenParser> stopParsers = new HashMap<>();
            Object parserLock = new Object();

            server.attachStreamRaw((routingId, payloadMessage) -> {
                try (Message payload = payloadMessage) {
                    int size = payload.size();
                    if (isStreamControl(payload, size)) {
                        return 0;
                    }

                    synchronized (parserLock) {
                        Len32StopTokenParser parser = stopParsers.computeIfAbsent(
                            routingId, ignored -> new Len32StopTokenParser());
                        if (isStopToken(payload, size)
                            || parser.consume(payload, size)) {
                            stopRequested.set(true);
                            return 0;
                        }
                    }

                    payloadSeen.incrementAndGet();
                    lastActivityNs.set(System.nanoTime());
                    int sent = server.streamSend(routingId, payload, SendFlag.NONE);
                    if (sent <= 0) {
                        callbackFailed.set(true);
                        stopRequested.set(true);
                        return 1;
                    }
                    return 0;
                } catch (RuntimeException ex) {
                    callbackFailed.set(true);
                    stopRequested.set(true);
                    return 1;
                }
            });

            server.bind(endpoint);
            System.out.println("READY," + endpoint);

            while (!stopRequested.get()) {
                if (callbackFailed.get()) {
                    return 2;
                }

                long seen = payloadSeen.get();
                long nowNs = System.nanoTime();
                if (seen > 0) {
                    if (nowNs - lastActivityNs.get() >= idleBreakNs) {
                        break;
                    }
                } else if (nowNs >= firstPacketDeadlineNs) {
                    break;
                }

                PerfCommon.sleepMillis(1);
            }

            try {
                server.detachStream();
            } catch (RuntimeException ignored) {
            }

            return callbackFailed.get() ? 2 : 0;
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

    private static boolean isStopToken(Message payload, int size) {
        if (size != STOP_TOKEN.length) {
            return false;
        }
        MemorySegment data = payload.dataSegment();
        for (int i = 0; i < STOP_TOKEN.length; i++) {
            if (data.get(ValueLayout.JAVA_BYTE, i) != STOP_TOKEN[i]) {
                return false;
            }
        }
        return true;
    }

    private static boolean isStreamControl(Message payload, int size) {
        if (size == 0) {
            return true;
        }
        if (size != 1) {
            return false;
        }
        byte value = payload.dataSegment().get(ValueLayout.JAVA_BYTE, 0);
        return value == 0 || value == 1;
    }

    private static final class Len32StopTokenParser {
        private byte[] buffer = new byte[2048];
        private int start = 0;
        private int end = 0;

        boolean consume(Message payload, int size) {
            if (size <= 0) {
                return false;
            }
            ensureCapacity(size);
            payload.copyTo(buffer, end);
            end += size;

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
