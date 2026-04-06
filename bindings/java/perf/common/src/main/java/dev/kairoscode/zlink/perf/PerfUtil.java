/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.Socket;
import java.lang.management.ManagementFactory;
import java.net.ServerSocket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
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
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

public final class PerfUtil {
    public static final int SINGLE_MAGIC = 0x53504631; // SPF1
    public static final int MULTI_MAGIC = 0x4d504631; // MPF1
    public static final int PHASE_UNKNOWN = 0;
    public static final int PHASE_WARMUP = 1;
    public static final int PHASE_ACTIVE = 2;
    public static final int PHASE_DRAIN = 3;
    public static final int HEADER_SIZE = 32;
    private static final int GENERIC_MAGIC = 0x50455246; // PERF

    private static final int MAX_LATENCY_SAMPLES = 4 * 1024 * 1024;
    private static final int LATENCY_QUEUE_CAPACITY = 1 << 20;
    private static final DateTimeFormatter FILE_TS =
        DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss");
    private static final long BASE_EPOCH_US = System.currentTimeMillis() * 1_000L;
    private static final long BASE_NANO = System.nanoTime();
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

    public record MetricHeader(
        int magic,
        int runId,
        int phase,
        int msgSize,
        long seq,
        long sentTsUs
    ) {
    }

    public static final class Result {
        private final String status;
        private final String reason;
        private final String pattern;
        private final String transport;
        private final int size;
        private final double throughput;
        private final double bandwidth;
        private final double latencyMean;
        private final double latencyP95;
        private final double latencyP99;
        private final double cpuPct;
        private final double memMb;

        public Result(String status, String reason, String pattern, String transport,
                      int size, double throughput, double bandwidth,
                      double latencyMean, double latencyP95, double latencyP99,
                      double cpuPct, double memMb) {
            this.status = status;
            this.reason = reason;
            this.pattern = pattern;
            this.transport = transport;
            this.size = size;
            this.throughput = throughput;
            this.bandwidth = bandwidth;
            this.latencyMean = latencyMean;
            this.latencyP95 = latencyP95;
            this.latencyP99 = latencyP99;
            this.cpuPct = cpuPct;
            this.memMb = memMb;
        }

        public static Result unsupported(String reason, Config config) {
            return new Result("unsupported", reason, config.pattern(),
                config.transport(), config.size(), 0.0d, 0.0d, 0.0d, 0.0d,
                0.0d, Double.NaN, Double.NaN);
        }

        public String status() {
            return status;
        }

        public String toLine() {
            if ("unsupported".equals(status)) {
                return String.format(Locale.ROOT, "UNSUPPORTED,java,%s,%s,%s",
                    pattern, transport, sanitize(reason));
            }
            if (!"ok".equals(status)) {
                return String.format(Locale.ROOT, "FAIL,java,%s,%s,%s,%s",
                    pattern, transport, size, sanitize(reason));
            }
            String key = String.format(Locale.ROOT, "RESULT,current,%s,%s,%d",
                pattern, transport, size);
            return String.join(System.lineSeparator(),
                metricLine(key, "throughput", throughput),
                metricLine(key, "bandwidth", bandwidth),
                metricLine(key, "latency", latencyMean),
                metricLine(key, "latency_p95", latencyP95),
                metricLine(key, "latency_p99", latencyP99),
                metricLine(key, "cpu_pct", cpuPct),
                metricLine(key, "mem_mb", memMb),
                metricLine(key, "snd_pending_max", 0.0d),
                metricLine(key, "rcv_pending_max", 0.0d),
                metricLine(key, "rcv_pending_end", 0.0d));
        }

        private static String metricLine(String key, String metric, double value) {
            return String.format(Locale.ROOT, "%s,%s,%s", key, metric, metric(value));
        }
    }

    public static final class Metrics {
        private long[] latencies = new long[Math.min(MAX_LATENCY_SAMPLES, 1 << 20)];
        private int size;
        private long count;
        private long sum;
        private long sampleStride = 1L;
        private long cpuStartNanos;
        private long wallStartNanos;
        private long memStartBytes;

        public void startResourceWindow() {
            wallStartNanos = System.nanoTime();
            cpuStartNanos = OS_BEAN == null ? 0L : OS_BEAN.getProcessCpuTime();
            memStartBytes = usedMemoryBytes();
        }

        public synchronized void recordMicros(long value) {
            count++;
            sum += value;
            if ((count % sampleStride) != 0L) {
                return;
            }
            ensureCapacity();
            if ((count % sampleStride) != 0L) {
                return;
            }
            latencies[size++] = value;
        }

        public void recordMillis(double value) {
            recordMicros(Math.round(value * 1000.0d));
        }

        public synchronized Result finishSingle(Config config) {
            return finish(config, 1.0d);
        }

        public synchronized Result finishMulti(Config config) {
            return finish(config, 1000.0d);
        }

        private Result finish(Config config, double latencyDivisor) {
            if (count == 0) {
                return new Result("fail", "no_active_samples", config.pattern(),
                    config.transport(), config.size(), 0.0d, 0.0d, 0.0d,
                    0.0d, 0.0d, Double.NaN, bytesToMb(usedMemoryBytes()));
            }
            long[] sorted = Arrays.copyOf(latencies, size);
            Arrays.sort(sorted);
            double throughput = count / (double) config.durationSeconds();
            double bandwidth = throughput * config.size()
                * (isEchoPattern(config.pattern()) ? 2.0d : 1.0d)
                / 1_000_000.0d;
            double mean = (sum / (double) count) / latencyDivisor;
            double p95 = sorted[index(sorted.length, 0.95d)] / latencyDivisor;
            double p99 = sorted[index(sorted.length, 0.99d)] / latencyDivisor;
            long wallDelta = Math.max(1L, System.nanoTime() - wallStartNanos);
            long cpuDelta = OS_BEAN == null ? 0L
                : Math.max(0L, OS_BEAN.getProcessCpuTime() - cpuStartNanos);
            double cpuPct = OS_BEAN == null ? Double.NaN
                : 100.0d * cpuDelta
                    / (wallDelta * Runtime.getRuntime().availableProcessors());
            double memMb = bytesToMb(Math.max(memStartBytes, usedMemoryBytes()));
            return new Result("ok", "-", config.pattern(), config.transport(),
                config.size(), throughput, bandwidth, mean, p95, p99, cpuPct,
                memMb);
        }

        private void ensureCapacity() {
            if (size < latencies.length) {
                return;
            }
            if (latencies.length < MAX_LATENCY_SAMPLES) {
                int next = Math.min(MAX_LATENCY_SAMPLES, latencies.length * 2);
                latencies = Arrays.copyOf(latencies, next);
                return;
            }
            int compacted = 0;
            for (int i = 0; i < size; i += 2) {
                latencies[compacted++] = latencies[i];
            }
            size = compacted;
            sampleStride *= 2L;
        }
    }

    public static boolean isEchoPattern(String pattern) {
        return "DEALER_ROUTER".equals(pattern)
            || "ROUTER_ROUTER".equals(pattern)
            || "STREAM".equals(pattern);
    }

    public static final class LatencyCollector implements AutoCloseable {
        private final long[] queue = new long[LATENCY_QUEUE_CAPACITY];
        private final AtomicLong writeIndex = new AtomicLong();
        private final AtomicLong readIndex = new AtomicLong();
        private final AtomicReference<RuntimeException> failure = new AtomicReference<>();
        private volatile boolean closed;
        private Thread worker;

        public void start(Metrics metrics, String threadName) {
            worker = new Thread(() -> runWorker(metrics), threadName);
            worker.setDaemon(true);
            worker.start();
        }

        public void offerMicros(long value) {
            long write = writeIndex.get();
            long read = readIndex.get();
            if (write - read >= queue.length) {
                failure.compareAndSet(null,
                    new IllegalStateException("latency queue overflow"));
                return;
            }
            queue[(int) (write & (queue.length - 1))] = value;
            writeIndex.lazySet(write + 1);
        }

        public void await(Duration timeout, String label) {
            closed = true;
            if (worker != null) {
                join(worker, label, timeout);
            }
            RuntimeException queuedFailure = failure.get();
            if (queuedFailure != null) {
                throw queuedFailure;
            }
        }

        private void runWorker(Metrics metrics) {
            while (!closed || readIndex.get() < writeIndex.get()) {
                long read = readIndex.get();
                long write = writeIndex.get();
                if (read >= write) {
                    Thread.onSpinWait();
                    continue;
                }
                long latencyUs = queue[(int) (read & (queue.length - 1))];
                readIndex.lazySet(read + 1);
                metrics.recordMicros(latencyUs);
            }
        }

        @Override
        public void close() {
            closed = true;
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

    public static int runId() {
        return (int) (System.nanoTime() & 0x7fff_ffffL);
    }

    public static Message singlePayload(int size, int phase, int runId, long seq) {
        return payload(SINGLE_MAGIC, size, phase, runId, seq);
    }

    public static Message multiPayload(int size, int phase, int runId, long seq) {
        return payload(MULTI_MAGIC, size, phase, runId, seq);
    }

    public static Message payload(int size, byte phase, long sentNanoTime) {
        long nowUs = BASE_EPOCH_US + (sentNanoTime - BASE_NANO) / 1_000L;
        return payload(GENERIC_MAGIC, size, phase, 0, 0L, nowUs);
    }

    private static Message payload(int magic, int size, int phase, int runId, long seq) {
        return payload(magic, size, phase, runId, seq, nowUs());
    }

    private static Message payload(int magic, int size, int phase, int runId, long seq,
                                   long sentTsUs) {
        int capacity = Math.max(size, HEADER_SIZE);
        ByteBuffer buffer = ByteBuffer.allocate(capacity).order(ByteOrder.LITTLE_ENDIAN);
        buffer.putInt(magic);
        buffer.putInt(runId);
        buffer.putInt(phase);
        buffer.putInt(size);
        buffer.putLong(seq);
        buffer.putLong(sentTsUs);
        while (buffer.hasRemaining()) {
            buffer.put((byte) 'a');
        }
        buffer.flip();
        return Message.copyOf(buffer);
    }

    public static MetricHeader decodeExpectedSingle(Message message, int runId,
                                                    int expectedSize) {
        return decodeExpected(message, SINGLE_MAGIC, runId, expectedSize);
    }

    public static MetricHeader decodeExpectedMulti(Message message, int runId,
                                                   int expectedSize) {
        return decodeExpected(message, MULTI_MAGIC, runId, expectedSize);
    }

    private static MetricHeader decodeExpected(Message message, int expectedMagic,
                                               int runId, int expectedSize) {
        if (message == null || message.size() < HEADER_SIZE) {
            return null;
        }
        int magic = message.readIntLe(0);
        int actualRunId = message.readIntLe(4);
        int phase = message.readIntLe(8);
        int msgSize = message.readIntLe(12);
        long seq = message.readLongLe(16);
        long sentTsUs = message.readLongLe(24);
        if (magic != expectedMagic || actualRunId != runId || msgSize != expectedSize) {
            return null;
        }
        return new MetricHeader(magic, actualRunId, phase, msgSize, seq, sentTsUs);
    }

    public static long latencyMicros(MetricHeader header) {
        return Math.max(0L, nowUs() - header.sentTsUs());
    }

    public static byte phase(Message message) {
        if (message == null || message.size() < HEADER_SIZE) {
            return (byte) PHASE_UNKNOWN;
        }
        return (byte) message.readIntLe(8);
    }

    public static long latencyMicros(Message message) {
        if (message == null || message.size() < HEADER_SIZE) {
            return 0L;
        }
        return Math.max(0L, nowUs() - message.readLongLe(24));
    }

    public static double latencyMillis(Message message) {
        return latencyMicros(message) / 1000.0d;
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

    public static void waitForReadySignal(int port) {
        if (port <= 0) {
            return;
        }
        try (ServerSocket server = new ServerSocket(port);
             java.net.Socket socket = server.accept()) {
            socket.setSoTimeout(10_000);
            if (socket.getInputStream().read() < 0) {
                throw new IllegalStateException("ready signal closed");
            }
        } catch (java.io.IOException ex) {
            throw new IllegalStateException("failed waiting for ready signal", ex);
        }
    }

    public static void sendReadySignal(int port) {
        if (port <= 0) {
            return;
        }
        long deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos();
        while (System.nanoTime() < deadline) {
            try (java.net.Socket socket = new java.net.Socket("127.0.0.1", port)) {
                socket.getOutputStream().write(1);
                socket.getOutputStream().flush();
                return;
            } catch (java.io.IOException ignored) {
                try {
                    Thread.sleep(25L);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("ready signal interrupted", ex);
                }
            }
        }
        throw new IllegalStateException("failed to send ready signal");
    }

    public static Path ensureResultsDir(Path root, String suite, String leaf) {
        Path dir = root.resolve(suite).resolve(leaf);
        try {
            Files.createDirectories(dir);
        } catch (java.io.IOException ex) {
            throw new IllegalStateException("failed to create results dir: " + dir, ex);
        }
        return dir;
    }

    public static String resultFileName(String platform, String recvMode, String tag) {
        String base = "perf_" + platform + "_" + recvMode + "_"
            + LocalDateTime.now().format(FILE_TS);
        return tag == null || tag.isBlank() ? base + ".txt" : base + "_" + tag + ".txt";
    }

    public static long nowUs() {
        return BASE_EPOCH_US + (System.nanoTime() - BASE_NANO) / 1_000L;
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

    private static String cert(String name) {
        return Path.of("tests", "certs", name).toAbsolutePath().toString();
    }
}
