/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.Message;
import java.net.ServerSocket;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;

final class PerfMeasurement {
    private static final int MAGIC = 0x5A4C4E4B;
    private static final long BASE_EPOCH_NS = System.currentTimeMillis() * 1_000_000L;
    private static final long BASE_NANO = System.nanoTime();
    private static final int RUN_ID = 1;
    private static final AtomicLong SEQ = new AtomicLong();

    private PerfMeasurement() {
    }

    static int runId() {
        return RUN_ID;
    }

    static Message payload(int size, byte phase, long sentNanoTime) {
        Message payload = payloadTemplate(size);
        writePayload(payload, size, phase, sentNanoTime);
        return payload;
    }

    static Message payloadTemplate(int size) {
        int capacity = Math.max(size, PerfUtil.HEADER_SIZE);
        Message payload = new Message(capacity);
        payload.fill((byte) 'a');
        payload.writeIntLe(0, MAGIC);
        payload.writeIntLe(4, RUN_ID);
        payload.writeIntLe(9, size);
        return payload;
    }

    static void writePayload(Message payload, int size, byte phase, long sentNanoTime) {
        long sentTsNs = BASE_EPOCH_NS + (sentNanoTime - BASE_NANO);
        payload.writeIntLe(0, MAGIC);
        payload.writeIntLe(4, RUN_ID);
        payload.writeByte(8, phase);
        payload.writeIntLe(9, size);
        payload.writeLongLe(13, SEQ.getAndIncrement());
        payload.writeLongLe(21, sentTsNs);
    }

    static byte phase(Message message) {
        if (message == null || message.size() < PerfUtil.HEADER_SIZE) {
            return (byte) PerfUtil.PHASE_UNKNOWN;
        }
        return message.dataBuffer().get(8);
    }

    static long latencyNanos(Message message) {
        if (message == null || message.size() < PerfUtil.HEADER_SIZE) {
            return 0L;
        }
        return Math.max(0L, nowNs() - message.readLongLe(21));
    }

    static double latencyMillis(Message message) {
        return latencyNanos(message) / 1_000_000.0d;
    }

    static String endpoint(String transport, String token) {
        return switch (transport) {
            case "tcp", "tls", "ws", "wss" -> transport + "://127.0.0.1:" + freePort();
            case "inproc" -> "inproc://perf-" + token + "-" + UUID.randomUUID();
            case "ipc" -> "ipc://" + ipcFile(token).toAbsolutePath();
            default -> throw new IllegalArgumentException("unsupported transport: " + transport);
        };
    }

    static String derivedControlEndpoint(String endpoint) {
        int schemeSep = endpoint.indexOf("://");
        int colon = endpoint.lastIndexOf(':');
        if (schemeSep <= 0 || colon <= schemeSep + 2 || colon == endpoint.length() - 1) {
            throw new IllegalArgumentException("cannot derive control endpoint from: " + endpoint);
        }
        int port = Integer.parseInt(endpoint.substring(colon + 1));
        return endpoint.substring(0, colon + 1) + (port + 1);
    }

    static boolean isEchoPattern(String pattern) {
        return "DEALER_ROUTER".equals(pattern)
            || "ROUTER_ROUTER".equals(pattern)
            || "STREAM".equals(pattern)
            || "MULTI_SPOT_REQREP".equals(pattern);
    }

    static long nowNs() {
        return BASE_EPOCH_NS + (System.nanoTime() - BASE_NANO);
    }

    static double bytesToMb(long bytes) {
        return bytes / 1_000_000.0d;
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
