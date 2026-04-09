/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Message;
import java.net.ServerSocket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.atomic.AtomicLong;

final class PerfMeasurement {
    private static final long BASE_EPOCH_US = System.currentTimeMillis() * 1_000L;
    private static final long BASE_NANO = System.nanoTime();
    private static final int RUN_ID = nextRunId();
    private static final AtomicLong SEQ = new AtomicLong();

    private PerfMeasurement() {
    }

    static int runId() {
        return RUN_ID;
    }

    static Message payload(int size, byte phase, long sentNanoTime) {
        long nowUs = BASE_EPOCH_US + (sentNanoTime - BASE_NANO) / 1_000L;
        return payload(0x5A4C4E4B, size, phase, RUN_ID, SEQ.getAndIncrement(), nowUs);
    }

    static byte phase(Message message) {
        if (message == null || message.size() < PerfUtil.HEADER_SIZE) {
            return (byte) PerfUtil.PHASE_UNKNOWN;
        }
        return (byte) (message.readIntLe(8) & 0xFF);
    }

    static long latencyMicros(Message message) {
        if (message == null || message.size() < PerfUtil.HEADER_SIZE) {
            return 0L;
        }
        return Math.max(0L, nowUs() - message.readLongLe(21));
    }

    static double latencyMillis(Message message) {
        return latencyMicros(message) / 1000.0d;
    }

    static String endpoint(String transport, String token) {
        return switch (transport) {
            case "tcp", "tls", "ws", "wss" -> transport + "://127.0.0.1:" + freePort();
            case "inproc" -> "inproc://perf-" + token + "-" + UUID.randomUUID();
            case "ipc" -> "ipc://" + ipcFile(token).toAbsolutePath();
            default -> throw new IllegalArgumentException("unsupported transport: " + transport);
        };
    }

    static boolean isEchoPattern(String pattern) {
        return "DEALER_ROUTER".equals(pattern)
            || "ROUTER_ROUTER".equals(pattern)
            || "STREAM".equals(pattern);
    }

    static long nowUs() {
        return BASE_EPOCH_US + (System.nanoTime() - BASE_NANO) / 1_000L;
    }

    static double bytesToMb(long bytes) {
        return bytes / 1_000_000.0d;
    }

    private static Message payload(int magic, int size, int phase, int runId, long seq,
                                   long sentTsUs) {
        int capacity = Math.max(size, PerfUtil.HEADER_SIZE);
        ByteBuffer buffer = ByteBuffer.allocate(capacity).order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(magic);
        buffer.putInt(runId);
        buffer.put((byte) phase);
        buffer.putInt(size);
        buffer.putLong(seq);
        buffer.putLong(sentTsUs);
        while (buffer.hasRemaining()) {
            buffer.put((byte) 'a');
        }
        buffer.flip();
        return Message.copyOf(buffer);
    }

    private static int nextRunId() {
        int value = ThreadLocalRandom.current().nextInt();
        return value == 0 ? 1 : value;
    }

    private static int freePort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        } catch (java.io.IOException ex) {
            throw new IllegalStateException("failed to allocate port", ex);
        }
    }

    private static Path ipcFile(String token) {
        Path dir = Path.of(System.getProperty("java.io.tmpdir"), "zlink-java-perf-ipc");
        try {
            Files.createDirectories(dir);
        } catch (java.io.IOException ex) {
            throw new IllegalStateException("failed to create ipc dir", ex);
        }
        String safeToken = token.toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9]+", "");
        if (safeToken.length() > 8) {
            safeToken = safeToken.substring(0, 8);
        }
        String id = UUID.randomUUID().toString().replace("-", "").substring(0, 8);
        return dir.resolve("zj-" + safeToken + "-" + id + ".sock");
    }
}
