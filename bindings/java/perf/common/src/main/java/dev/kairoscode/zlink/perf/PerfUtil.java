/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.nio.file.Path;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;

public final class PerfUtil {
    public static final int PHASE_UNKNOWN = 0;
    public static final int PHASE_WARMUP = 1;
    public static final int PHASE_ACTIVE = 2;
    public static final int HEADER_SIZE = 32;

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

    public static final class Result {
        final String status;
        final String reason;
        final String pattern;
        final String transport;
        final int size;
        final double throughput;
        final double bandwidth;
        final double latencyMean;
        final double latencyP95;
        final double latencyP99;
        final double cpuPct;
        final double memMb;

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
            return PerfReport.format(this);
        }
    }

    public static final class Metrics {
        private final PerfMetricsCollector delegate = new PerfMetricsCollector();

        public void startResourceWindow() {
            delegate.startResourceWindow();
        }

        public synchronized void recordMicros(long value) {
            delegate.recordMicros(value);
        }

        public void recordMillis(double value) {
            delegate.recordMillis(value);
        }

        public synchronized Result finishSingle(Config config) {
            return delegate.finishSingle(config);
        }

        public synchronized Result finishMulti(Config config) {
            return delegate.finishMulti(config);
        }
    }

    public static Config parseSingleArgs(String[] args) {
        return PerfArgs.parseSingleArgs(args);
    }

    public static Config parseMultiArgs(String[] args) {
        return PerfArgs.parseMultiArgs(args);
    }

    public static int runId() {
        return PerfMeasurement.runId();
    }

    public static Message payload(int size, byte phase, long sentNanoTime) {
        return PerfMeasurement.payload(size, phase, sentNanoTime);
    }

    public static byte phase(Message message) {
        return PerfMeasurement.phase(message);
    }

    public static long latencyMicros(Message message) {
        return PerfMeasurement.latencyMicros(message);
    }

    public static double latencyMillis(Message message) {
        return PerfMeasurement.latencyMillis(message);
    }

    public static String endpoint(String transport, String token) {
        return PerfMeasurement.endpoint(transport, token);
    }

    public static boolean isEchoPattern(String pattern) {
        return PerfMeasurement.isEchoPattern(pattern);
    }

    public static void validateMultiRecvMode(Config config) {
        PerfPolicy.validateMultiRecvMode(config);
    }

    public static void configureServerTls(Socket socket, String transport) {
        PerfTransport.configureServerTls(socket, transport);
    }

    public static void configureClientTls(Socket socket, String transport) {
        PerfTransport.configureClientTls(socket, transport);
    }

    public static void configureServerTls(SpotNode node, String transport) {
        PerfTransport.configureServerTls(node, transport);
    }

    public static void configureClientTls(SpotNode node, String transport) {
        PerfTransport.configureClientTls(node, transport);
    }

    public static void await(CountDownLatch latch, String label, Duration timeout) {
        PerfTransport.await(latch, label, timeout);
    }

    public static void join(Thread thread, String label, Duration timeout) {
        PerfTransport.join(thread, label, timeout);
    }

    public static void waitForMonitorEvent(MonitorSocket monitor, long expectedMask,
                                           int expectedCount, Duration timeout,
                                           String label) {
        PerfTransport.waitForMonitorEvent(monitor, expectedMask, expectedCount, timeout, label);
    }

    public static void waitForReadySignal(int port) {
        PerfTransport.waitForReadySignal(port);
    }

    public static void sendReadySignal(int port) {
        PerfTransport.sendReadySignal(port);
    }

    public static Path ensureResultsDir(Path root, String suite, String leaf) {
        return PerfReport.ensureResultsDir(root, suite, leaf);
    }

    public static String resultFileName(String platform, String recvMode, String tag) {
        return PerfReport.resultFileName(platform, recvMode, tag);
    }

    public static long nowUs() {
        return PerfMeasurement.nowUs();
    }
}
