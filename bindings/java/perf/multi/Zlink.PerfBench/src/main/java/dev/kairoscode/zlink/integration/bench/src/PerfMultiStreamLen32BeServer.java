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
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

/**
 * MULTI_STREAM_LEN32BE server benchmark.
 * STREAM(bind) with attachStreamLen32be(batch handler), echoes payload and exits on stop token.
 */
public final class PerfMultiStreamLen32BeServer {
    private static final String PATTERN = "MULTI_STREAM_LEN32BE";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private static final long NANOSECONDS_PER_MILLISECOND = 1_000_000L;

    private PerfMultiStreamLen32BeServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        String endpoint = resolveServerEndpoint(transport,
            "multi-stream-len32be");

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

            server.attachStreamLen32be((routingId, packets) -> {
                if (packets == null || packets.length == 0) {
                    return 0;
                }

                for (int i = 0; i < packets.length; i++) {
                    Message packet = packets[i];
                    if (packet == null) {
                        continue;
                    }
                    int size = packet.size();
                    if (isStreamControl(packet, size)) {
                        packet.close();
                        continue;
                    }
                    if (isStopToken(packet, size)) {
                        stopRequested.set(true);
                        packet.close();
                        continue;
                    }

                    payloadSeen.incrementAndGet();
                    lastActivityNs.set(System.nanoTime());
                    try {
                        int sent = server.streamSend(routingId,
                            packet.dataSegment(), SendFlag.NONE);
                        if (sent <= 0) {
                            callbackFailed.set(true);
                            stopRequested.set(true);
                            packet.close();
                            return 1;
                        }
                    } catch (RuntimeException ex) {
                        callbackFailed.set(true);
                        stopRequested.set(true);
                        packet.close();
                        return 1;
                    }
                }
                return 0;
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
}
