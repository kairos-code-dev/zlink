/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.Socket;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.OutputStream;
import java.lang.management.ManagementFactory;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class PerfUtil {
    private static final byte PHASE_ACTIVE = 0;
    private static final byte PHASE_STOP = 1;
    private static final byte PHASE_WARMUP = 2;
    private static final int HEADER_SIZE = 9;
    private static final DateTimeFormatter FILE_TS =
        DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss");
    private static final com.sun.management.OperatingSystemMXBean OS_BEAN =
        ManagementFactory.getOperatingSystemMXBean()
            instanceof com.sun.management.OperatingSystemMXBean bean ? bean : null;

    private PerfUtil() {
    }

    public record Config(
        String pattern,
        String transport,
        int size,
        int warmupSeconds,
        int durationSeconds,
        String recvMode,
        String endpoint,
        int clients,
        int controlPort
    ) {
    }

    public record Result(
        String status,
        String reason,
        double throughput,
        double bandwidth,
        double latencyMean,
        double latencyP95,
        double latencyP99,
        double cpuPct,
        double memMb,
        String latencyUnit
    ) {
        public String toLine() {
            return String.format(Locale.ROOT,
                "RESULT status=%s reason=%s throughput=%.2f bandwidth=%.2f "
                    + "lat_mean=%.2f lat_p95=%.2f lat_p99=%.2f lat_unit=%s "
                    + "cpu_pct=%s mem_mb=%s",
                status,
                sanitize(reason),
                throughput,
                bandwidth,
                latencyMean,
                latencyP95,
                latencyP99,
                latencyUnit,
                metric(cpuPct),
                metric(memMb));
        }
    }

    public static final class Metrics {
        private long[] latencies = new long[1024];
        private int size;
        private long count;
        private long sum;
        private long cpuStartNanos;
        private long wallStartNanos;
        private long memStartBytes;

        public void startResourceWindow() {
            wallStartNanos = System.nanoTime();
            cpuStartNanos = OS_BEAN == null ? 0L : OS_BEAN.getProcessCpuTime();
            memStartBytes = usedMemoryBytes();
        }

        public synchronized void recordMicros(long value) {
            if (size == latencies.length) {
                latencies = Arrays.copyOf(latencies, latencies.length * 2);
            }
            latencies[size++] = value;
            count++;
            sum += value;
        }

        public synchronized void recordMillis(double value) {
            recordMicros(Math.round(value * 1000.0d));
        }

        public synchronized Result finishSingle(int payloadSize, int durationSeconds) {
            return finish(payloadSize, durationSeconds, "us", 1.0d);
        }

        public synchronized Result finishMulti(int payloadSize, int durationSeconds) {
            return finish(payloadSize, durationSeconds, "ms", 1000.0d);
        }

        private Result finish(int payloadSize, int durationSeconds,
                              String latencyUnit, double divisor) {
            if (count == 0) {
                return new Result("timeout", "no_active_samples", 0.0d, 0.0d,
                    0.0d, 0.0d, 0.0d, Double.NaN, bytesToMb(usedMemoryBytes()),
                    latencyUnit);
            }
            long[] copy = Arrays.copyOf(latencies, size);
            Arrays.sort(copy);
            double mean = (sum / (double) count) / divisor;
            double p95 = copy[index(copy.length, 0.95d)] / divisor;
            double p99 = copy[index(copy.length, 0.99d)] / divisor;
            double throughput = count / (double) durationSeconds;
            double bandwidth = throughput * payloadSize / 1_000_000.0d;
            long wallDelta = Math.max(1L, System.nanoTime() - wallStartNanos);
            long cpuDelta = OS_BEAN == null ? 0L : Math.max(0L,
                OS_BEAN.getProcessCpuTime() - cpuStartNanos);
            double cpuPct = OS_BEAN == null ? Double.NaN
                : 100.0d * cpuDelta / (wallDelta * Runtime.getRuntime().availableProcessors());
            double memMb = bytesToMb(Math.max(memStartBytes, usedMemoryBytes()));
            return new Result("ok", "-", throughput, bandwidth, mean, p95, p99,
                cpuPct, memMb, latencyUnit);
        }
    }

    public static Config parseSingleArgs(String[] args) {
        if (args.length < 3) {
            throw new IllegalArgumentException("usage: <pattern> <transport> <size>");
        }
        String pattern = args[0].toUpperCase(Locale.ROOT);
        String transport = args[1].toLowerCase(Locale.ROOT);
        int size = Integer.parseInt(args[2]);
        int warmup = 2;
        int duration = 5;
        String recv = "callback";
        for (int i = 3; i + 1 < args.length; i += 2) {
            switch (args[i]) {
                case "--warmup" -> warmup = Integer.parseInt(args[i + 1]);
                case "--duration" -> duration = Integer.parseInt(args[i + 1]);
                case "--recv" -> recv = args[i + 1];
                default -> {
                }
            }
        }
        return new Config(pattern, transport, size, warmup, duration, recv,
            "", 1, 0);
    }

    public static Config parseMultiArgs(String[] args) {
        if (args.length < 8) {
            throw new IllegalArgumentException("usage: --multi-(server|client) ...");
        }
        String pattern = args[1].toUpperCase(Locale.ROOT);
        if (pattern.startsWith("MULTI_")) {
            pattern = pattern.substring("MULTI_".length());
        }
        String transport = args[2].toLowerCase(Locale.ROOT);
        int size = Integer.parseInt(args[3]);
        int warmup = 2;
        int duration = 5;
        String recv = "recv";
        String endpoint = "";
        int clients = 32;
        int controlPort = 0;
        for (int i = 4; i + 1 < args.length; i += 2) {
            switch (args[i]) {
                case "--warmup" -> warmup = Integer.parseInt(args[i + 1]);
                case "--duration" -> duration = Integer.parseInt(args[i + 1]);
                case "--recv" -> recv = args[i + 1];
                case "--endpoint" -> endpoint = args[i + 1];
                case "--clients" -> clients = Integer.parseInt(args[i + 1]);
                case "--control-port" -> controlPort = Integer.parseInt(args[i + 1]);
                default -> {
                }
            }
        }
        return new Config(pattern, transport, size, warmup, duration, recv,
            endpoint, clients, controlPort);
    }

    public static Message payload(int size, byte phase, long sendNanos) {
        int capacity = Math.max(size, HEADER_SIZE);
        ByteBuffer buffer = ByteBuffer.allocate(capacity).order(ByteOrder.LITTLE_ENDIAN);
        buffer.put(phase);
        buffer.putLong(sendNanos);
        while (buffer.hasRemaining()) {
            buffer.put((byte) 'a');
        }
        return Message.copyOf(buffer.flip());
    }

    public static byte phase(Message message) {
        byte[] data = message.toByteArray();
        return data.length == 0 ? PHASE_ACTIVE : data[0];
    }

    public static long sentNanos(Message message) {
        byte[] data = message.toByteArray();
        if (data.length < HEADER_SIZE) {
            return 0L;
        }
        return ByteBuffer.wrap(data, 1, 8).order(ByteOrder.LITTLE_ENDIAN).getLong();
    }

    public static long latencyMicros(Message message) {
        return Math.max(0L, (System.nanoTime() - sentNanos(message)) / 1_000L);
    }

    public static double latencyMillis(Message message) {
        return Math.max(0L, (System.nanoTime() - sentNanos(message)) / 1_000_000.0d);
    }

    public static String endpoint(String transport, String token) {
        return switch (transport) {
            case "tcp", "tls", "ws", "wss" -> transport + "://127.0.0.1:" + freePort();
            case "inproc" -> "inproc://perf-" + token + "-" + UUID.randomUUID();
            case "ipc" -> "ipc://" + ipcFile(token).toAbsolutePath();
            default -> throw new IllegalArgumentException("unsupported transport: " + transport);
        };
    }

    public static void configureServerTls(Socket socket, String transport) {
        if (!transport.equals("tls") && !transport.equals("wss")) {
            return;
        }
        socket.setTlsServer(cert("server.crt"), cert("server.key"), false);
    }

    public static void configureClientTls(Socket socket, String transport) {
        if (!transport.equals("tls") && !transport.equals("wss")) {
            return;
        }
        socket.setTlsClient(cert("ca.crt"), "localhost", true);
    }

    public static void await(CountDownLatch latch, String label, Duration timeout) {
        try {
            if (!latch.await(timeout.toMillis(), TimeUnit.MILLISECONDS)) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    public static void join(Thread thread, String label, Duration timeout) {
        try {
            thread.join(timeout.toMillis());
            if (thread.isAlive()) {
                throw new IllegalStateException(label + " timed out");
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException(label + " interrupted", ex);
        }
    }

    public static void waitForReadySignal(int controlPort) {
        try (ServerSocket server = new ServerSocket()) {
            server.bind(new InetSocketAddress("127.0.0.1", controlPort));
            server.setSoTimeout(20_000);
            try (java.net.Socket socket = server.accept();
                 BufferedReader reader = socketToReader(socket)) {
                String line = reader.readLine();
                if (!"READY".equals(line)) {
                    throw new IllegalStateException("invalid ready signal: " + line);
                }
            }
        } catch (SocketTimeoutException ex) {
            throw new IllegalStateException("ready signal timed out", ex);
        } catch (IOException ex) {
            throw new IllegalStateException("ready signal failed", ex);
        }
    }

    public static void sendReadySignal(int controlPort) {
        long deadline = System.nanoTime() + 20_000_000_000L;
        while (System.nanoTime() < deadline) {
            try (java.net.Socket socket = new java.net.Socket()) {
                socket.connect(new InetSocketAddress("127.0.0.1", controlPort), 1_000);
                OutputStream out = socket.getOutputStream();
                out.write("READY\n".getBytes(StandardCharsets.UTF_8));
                out.flush();
                return;
            } catch (IOException ex) {
                try {
                    Thread.sleep(50L);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("failed to send ready signal",
                        interrupted);
                }
            }
        }
        throw new IllegalStateException("failed to send ready signal");
    }

    public static void waitForMonitorEvent(MonitorSocket monitor, long expectedMask,
                                           int expectedCount, Duration timeout,
                                           String label) {
        final RuntimeException[] failure = new RuntimeException[1];
        Thread waiter = new Thread(() -> {
            int seen = 0;
            while (seen < expectedCount) {
                MonitorEvent event = monitor.recv();
                if ((event.event() & expectedMask) == 0L) {
                    failure[0] = new IllegalStateException(label
                        + " unexpected event: " + event.event());
                    return;
                }
                seen++;
            }
        }, "perf-monitor-wait");
        waiter.setDaemon(true);
        waiter.start();
        join(waiter, label, timeout);
        if (failure[0] != null) {
            throw failure[0];
        }
    }

    public static Path ensureResultsDir(Path root, String suite, String leaf) {
        Path dir = root.resolve(suite).resolve(leaf);
        try {
            Files.createDirectories(dir);
        } catch (IOException ex) {
            throw new IllegalStateException("failed to create results dir: " + dir, ex);
        }
        return dir;
    }

    public static String resultFileName(String platform, String recvMode, String tag) {
        String base = "perf_" + platform + "_" + recvMode + "_"
            + LocalDateTime.now().format(FILE_TS);
        return tag == null || tag.isBlank() ? base + ".txt" : base + "_" + tag + ".txt";
    }

    private static String metric(double value) {
        return Double.isNaN(value) ? "N/A" : String.format(Locale.ROOT, "%.2f", value);
    }

    private static String sanitize(String value) {
        return value == null || value.isBlank() ? "-" : value.replace(' ', '_');
    }

    private static double bytesToMb(long bytes) {
        return bytes / 1_000_000.0d;
    }

    private static int index(int length, double percentile) {
        return Math.max(0, Math.min(length - 1,
            (int) Math.ceil(length * percentile) - 1));
    }

    private static long usedMemoryBytes() {
        Runtime runtime = Runtime.getRuntime();
        return runtime.totalMemory() - runtime.freeMemory();
    }

    private static int freePort() {
        try (ServerSocket socket = new ServerSocket(0)) {
            return socket.getLocalPort();
        } catch (IOException ex) {
            throw new IllegalStateException("failed to allocate port", ex);
        }
    }

    private static Path ipcFile(String token) {
        Path dir = Path.of(System.getProperty("java.io.tmpdir"), "zlink-java-perf-ipc");
        try {
            Files.createDirectories(dir);
        } catch (IOException ex) {
            throw new IllegalStateException("failed to create ipc dir", ex);
        }
        String safeToken = token.toLowerCase(Locale.ROOT)
            .replaceAll("[^a-z0-9]+", "");
        if (safeToken.length() > 8) {
            safeToken = safeToken.substring(0, 8);
        }
        String id = UUID.randomUUID().toString().replace("-", "").substring(0, 8);
        return dir.resolve("zj-" + safeToken + "-" + id + ".sock");
    }

    private static String cert(String name) {
        return Path.of("tests", "certs", name).toAbsolutePath().toString();
    }

    private static BufferedReader socketToReader(java.net.Socket socket) throws IOException {
        return new BufferedReader(new java.io.InputStreamReader(socket.getInputStream(),
            StandardCharsets.UTF_8));
    }
}
