/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ContextOption;
import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.Poller;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.options.SocketOptions;
import java.io.IOException;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.function.BooleanSupplier;

public final class PerfCommon {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;

    private static final double US_PER_MS = 1_000.0;
    private static final long US_PER_SEC = 1_000_000L;
    private static final Object IPC_LOCK = new Object();
    private static final Set<String> IPC_PATHS = new HashSet<>();
    private static volatile boolean ipcShutdownHooked = false;
    private static final ThreadLocal<Poller> WAIT_INPUT_POLLER =
        ThreadLocal.withInitial(Poller::new);

    private PerfCommon() {
    }

    public static int parseEnv(String name, int defaultValue) {
        String raw = System.getenv(name);
        if (raw == null || raw.isBlank()) {
            return defaultValue;
        }
        try {
            int parsed = Integer.parseInt(raw.trim());
            return parsed > 0 ? parsed : defaultValue;
        } catch (NumberFormatException ex) {
            return defaultValue;
        }
    }

    public static int parseEnvNonNegative(String name, int defaultValue) {
        String raw = System.getenv(name);
        if (raw == null || raw.isBlank()) {
            return defaultValue;
        }
        try {
            int parsed = Integer.parseInt(raw.trim());
            return parsed >= 0 ? parsed : defaultValue;
        } catch (NumberFormatException ex) {
            return defaultValue;
        }
    }

    public static void applySingleContextOptions(Context context) {
        int ioThreads = parseEnvNonNegative("PERF_IO_THREADS", 0);
        if (ioThreads > 0) {
            context.setOption(ContextOption.IO_THREADS, ioThreads);
        }

        int maxSockets = parseEnvNonNegative("PERF_MAX_SOCKETS", 0);
        if (maxSockets > 0) {
            context.setOption(ContextOption.MAX_SOCKETS, maxSockets);
        }
    }

    public static void applySingleSocketOptions(Socket socket) {
        int baseHwm = parseEnvNonNegative("PERF_SINGLE_HWM", 0);

        int sndHwm = parseEnvNonNegative("PERF_SINGLE_SNDHWM", 0);
        if (sndHwm <= 0) {
            sndHwm = baseHwm > 0 ? baseHwm : 1000;
        }

        int rcvHwm = parseEnvNonNegative("PERF_SINGLE_RCVHWM", 0);
        if (rcvHwm <= 0) {
            rcvHwm = baseHwm > 0 ? baseHwm : 1000;
        }

        int sndTimeo = parseEnvNonNegative("PERF_SINGLE_SNDTIMEO_MS", 200);
        int rcvTimeo = parseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);

        socket.setOption(SocketOptions.SNDHWM, sndHwm);
        socket.setOption(SocketOptions.RCVHWM, rcvHwm);
        socket.setOption(SocketOptions.SNDTIMEO, sndTimeo);
        socket.setOption(SocketOptions.RCVTIMEO, rcvTimeo);
    }

    public static boolean waitForInput(Socket socket, int timeoutMs) {
        Poller poller = WAIT_INPUT_POLLER.get();
        poller.clear();
        poller.add(socket, PollEventType.POLLIN);
        return poller.pollCount(timeoutMs) > 0;
    }

    public static boolean waitMonitorReady(MonitorSocket monitor, int timeoutMs,
                                           boolean acceptFallback) {
        long deadline = System.nanoTime() + Duration.ofMillis(
            Math.max(timeoutMs, 1)).toNanos();
        while (System.nanoTime() < deadline) {
            try {
                MonitorEvent event = monitor.recv(ReceiveFlag.DONTWAIT);
                long mask = event.event();
                if ((mask & MonitorEventType.CONNECTION_READY.getValue()) != 0) {
                    return true;
                }
                if (acceptFallback
                    && (((mask & MonitorEventType.ACCEPTED.getValue()) != 0)
                    || ((mask & MonitorEventType.CONNECTED.getValue()) != 0))) {
                    return true;
                }
            } catch (ZlinkException ex) {
                if (isInterrupted(ex.errno()) || isWouldBlock(ex.errno())) {
                    sleepMillis(1);
                    continue;
                }
                return false;
            }
        }
        return false;
    }

    public static boolean waitConnectReady(String transport,
                                           MonitorSocket monitor,
                                           int timeoutMs,
                                           boolean acceptFallback) {
        if ("inproc".equals(normalizeTransport(transport))) {
            return true;
        }
        return waitMonitorReady(monitor, timeoutMs, acceptFallback);
    }

    public static boolean waitUntil(BooleanSupplier check, int timeoutMs,
                                    int intervalMs) {
        long deadline = System.nanoTime() + Duration.ofMillis(
            Math.max(timeoutMs, 1)).toNanos();
        while (System.nanoTime() < deadline) {
            try {
                if (check.getAsBoolean()) {
                    return true;
                }
            } catch (RuntimeException ignored) {
            }
            sleepMillis(Math.max(intervalMs, 1));
        }
        return false;
    }

    public static String endpointFor(String transport, String name) {
        String tr = normalizeTransport(transport);
        String n = name == null || name.isBlank() ? "bench" : name;

        if ("inproc".equals(tr)) {
            return "inproc://bench-" + n + "-" + System.nanoTime();
        }

        if ("ipc".equals(tr)) {
            String path = "/tmp/zlink-java-perf-" + n + "-" + randomPort()
                + ".sock";
            registerIpcPath(path);
            return "ipc://" + path;
        }

        return tr + "://127.0.0.1:" + randomPort();
    }

    public static void printResult(String pattern, String transport, int size,
                                   double throughput, double latencyUs,
                                   double latencyP95Us,
                                   double latencyP99Us) {
        printResult(pattern, transport, size, throughput, latencyUs,
            latencyP95Us, latencyP99Us, 1.0);
    }

    public static void printResult(String pattern, String transport, int size,
                                   double throughput, double latencyUs,
                                   double latencyP95Us,
                                   double latencyP99Us,
                                   double bandwidthMultiplier) {
        double safeThroughput = Math.max(0.0, throughput);
        double safeLatencyUs = Math.max(0.0, latencyUs);
        double safeP95Us = Math.max(safeLatencyUs, latencyP95Us);
        double safeP99Us = Math.max(safeP95Us, latencyP99Us);
        double safeLatencyMs = safeLatencyUs / US_PER_MS;
        double safeP95Ms = safeP95Us / US_PER_MS;
        double safeP99Ms = safeP99Us / US_PER_MS;
        double bw = (safeThroughput * size * bandwidthMultiplier) / 1_000_000.0;

        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",throughput," + safeThroughput);
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",bandwidth," + bw);
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",latency," + safeLatencyMs);
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",latency_p95," + safeP95Ms);
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",latency_p99," + safeP99Ms);
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",snd_pending_max,0");
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",rcv_pending_max,0");
        System.out.println("RESULT,current," + pattern + "," + transport + ","
            + size + ",rcv_pending_end,0");
    }

    public static void printUnsupported(String pattern, String transport,
                                        int size, String reason) {
        String msg = reason == null ? "unsupported" : reason;
        System.out.println(
            "UNSUPPORTED," + pattern + "," + transport + "," + size + "," + msg);
    }

    public static int resolveWarmupCount(String pattern, int msgSize) {
        int fallback = "SPOT".equals(pattern) ? 200 : 1000;
        int configured = parseEnv("PERF_WARMUP_COUNT", fallback);
        if ("SPOT".equals(pattern) && msgSize >= 65_536) {
            return Math.min(configured, 20);
        }
        return configured;
    }

    public static int resolveDurationSeconds() {
        return parseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
    }

    public static int resolveRecvTimeoutMs() {
        return parseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
    }

    public static int resolveSettleMs() {
        return parseEnvNonNegative("PERF_SINGLE_SETTLE_MS", 100);
    }

    public static int resolveLatencySampleCap() {
        return parseEnv("PERF_SINGLE_LATENCY_SAMPLE_CAP", 200_000);
    }

    public static int resolveSpotDiscoveryTimeoutMs() {
        return parseEnvNonNegative("PERF_SPOT_DISCOVERY_TIMEOUT_MS", 4000);
    }

    public static int resolveSpotReadyTimeoutMs() {
        return parseEnvNonNegative("PERF_SPOT_READY_TIMEOUT_MS", 2000);
    }

    public static int randomRunId() {
        return (int) (PerfSingleMetricHeader.nowUs() & 0x7FFF_FFFFL);
    }

    private static int randomPort() {
        try (ServerSocket serverSocket = new ServerSocket(0)) {
            return serverSocket.getLocalPort();
        } catch (IOException ex) {
            throw new RuntimeException("cannot allocate local port", ex);
        }
    }

    private static String normalizeTransport(String transport) {
        if (transport == null) {
            return "tcp";
        }
        return transport.trim().toLowerCase();
    }

    private static void registerIpcPath(String path) {
        synchronized (IPC_LOCK) {
            IPC_PATHS.add(path);
            tryDeletePath(path);
            if (!ipcShutdownHooked) {
                Runtime.getRuntime().addShutdownHook(new Thread(
                    PerfCommon::cleanupIpcPaths));
                ipcShutdownHooked = true;
            }
        }
    }

    private static void cleanupIpcPaths() {
        List<String> snapshot;
        synchronized (IPC_LOCK) {
            snapshot = new ArrayList<>(IPC_PATHS);
        }
        for (String path : snapshot) {
            tryDeletePath(path);
        }
    }

    private static void tryDeletePath(String path) {
        try {
            Files.deleteIfExists(Path.of(path));
        } catch (IOException ignored) {
        }
    }

    private static void sleepMillis(int millis) {
        if (millis <= 0) {
            return;
        }
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new RuntimeException(ex);
        }
    }

    private static boolean isWouldBlock(int errno) {
        return errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN;
    }

    private static boolean isInterrupted(int errno) {
        return errno == ERRNO_EINTR;
    }

    public static final class LatencyReservoir {
        private final double[] samples;
        private int sampleCount;
        private long seen;
        private double sum;
        private long rngState;

        public LatencyReservoir(int cap) {
            int size = Math.max(1, cap);
            this.samples = new double[size];
            this.sampleCount = 0;
            this.seen = 0;
            this.sum = 0.0;
            this.rngState = 0x9E3779B97F4A7C15L;
        }

        public void add(double latencyUs) {
            double v = Math.max(0.0, latencyUs);
            seen++;
            sum += v;

            if (sampleCount < samples.length) {
                samples[sampleCount++] = v;
                return;
            }

            long slot = nextRandom() % seen;
            if (slot < samples.length) {
                samples[(int) slot] = v;
            }
        }

        public long count() {
            return seen;
        }

        public Stats snapshot() {
            if (seen == 0 || sampleCount == 0) {
                return new Stats(0.0, 0.0, 0.0);
            }

            double[] sorted = new double[sampleCount];
            System.arraycopy(samples, 0, sorted, 0, sampleCount);
            java.util.Arrays.sort(sorted);

            double mean = sum / seen;
            double p95 = percentile(sorted, 0.95);
            double p99 = percentile(sorted, 0.99);
            return new Stats(mean, p95, p99);
        }

        private double percentile(double[] sorted, double q) {
            if (sorted.length == 0) {
                return 0.0;
            }
            if (q <= 0.0) {
                return sorted[0];
            }
            if (q >= 1.0) {
                return sorted[sorted.length - 1];
            }
            double pos = (sorted.length - 1) * q;
            int lo = (int) pos;
            int hi = Math.min(lo + 1, sorted.length - 1);
            double frac = pos - lo;
            return sorted[lo] + ((sorted[hi] - sorted[lo]) * frac);
        }

        private long nextRandom() {
            long x = rngState;
            x ^= (x << 13);
            x ^= (x >>> 7);
            x ^= (x << 17);
            rngState = x;
            return x & Long.MAX_VALUE;
        }
    }

    public record Stats(double meanUs, double p95Us, double p99Us) {
    }

    public static byte[] utf8(String text) {
        return text.getBytes(StandardCharsets.UTF_8);
    }

    public static long toMicros(long startNs, long endNs) {
        long deltaNs = Math.max(0L, endNs - startNs);
        return deltaNs / (US_PER_SEC / 1000L);
    }
}
