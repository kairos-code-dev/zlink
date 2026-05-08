/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.Message;
import systems.zlink.MonitorSocket;
import systems.zlink.PairSocket;
import systems.zlink.DealerSocket;
import systems.zlink.RouterSocket;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.RecvResult;
import systems.zlink.Socket;
import systems.zlink.StreamSocket;
import systems.zlink.SubSocket;
import systems.zlink.TopicMessage;
import systems.zlink.Context;
import systems.zlink.service.spot.SpotNode;
import systems.zlink.service.spot.Spot;
import java.nio.file.Path;
import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;

public final class PerfUtil {
    public static final int PHASE_UNKNOWN = -1;
    public static final int PHASE_WARMUP = 0;
    public static final int PHASE_ACTIVE = 1;
    public static final int PHASE_COOLDOWN = 2;
    public static final int HEADER_SIZE = 29;

    private PerfUtil() {
    }

    public record Config(
        String suite,
        String pattern,
        String transport,
        int size,
        int durationSeconds,
        String endpoint,
        int clients,
        int ioThreads,
        int sendHwm,
        int recvHwm,
        int sendTimeoutMs,
        int recvTimeoutMs,
        int monitorHwm,
        int connectReadyTimeoutMs,
        int connectConcurrency
    ) {
    }

    public record Header(byte phase, long latencyNanos) {
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

        public Result(String status, String reason, String pattern, String transport,
                      int size, double throughput, double bandwidth,
                      double latencyMean, double latencyP95, double latencyP99) {
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
        }

        public static Result unsupported(String reason, Config config) {
            return new Result("unsupported", reason, config.pattern(),
                config.transport(), config.size(), 0.0d, 0.0d, 0.0d, 0.0d,
                0.0d);
        }

        public static Result silent(Config config) {
            return new Result("silent", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        }

        public String status() {
            return status;
        }

        public String toLine(String libTag) {
            return PerfReport.format(this, libTag);
        }
    }

    public static final class Metrics {
        private final PerfMetricsCollector delegate;

        public Metrics(Config config) {
            delegate = new PerfMetricsCollector(config.suite());
        }

        public void startActiveWindow() {
            delegate.startActiveWindow();
        }

        public synchronized void recordNanos(long value) {
            delegate.recordNanos(value);
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

    public static Message payloadTemplate(int size) {
        return PerfMeasurement.payloadTemplate(size);
    }

    public static void writePayload(Message payload, int size, byte phase,
                                    long sentNanoTime) {
        PerfMeasurement.writePayload(payload, size, phase, sentNanoTime);
    }

    public static byte phase(Message message) {
        return PerfMeasurement.phase(message);
    }

    public static long latencyNanos(Message message) {
        return PerfMeasurement.latencyNanos(message);
    }

    public static double latencyMillis(Message message) {
        return PerfMeasurement.latencyMillis(message);
    }

    public static Header decodeHeader(Message message, int expectedSize) {
        return PerfMetricHeader.decode(message, expectedSize);
    }

    public static String endpoint(String transport, String token) {
        return PerfMeasurement.endpoint(transport, token);
    }

    public static String derivedControlEndpoint(String endpoint) {
        return PerfMeasurement.derivedControlEndpoint(endpoint);
    }

    public static boolean isEchoPattern(String pattern) {
        return PerfMeasurement.isEchoPattern(pattern);
    }

    public static void validateMultiRecvMode(Config config) {
    }

    public static Result classifyFailure(Config config, Throwable failure) {
        return PerfPolicy.classifyFailure(config, failure);
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

    public static Context newContext(Config config) {
        return PerfTransport.newContext(config);
    }

    public static void applySocketOptions(Socket socket, Config config) {
        PerfTransport.applySocketOptions(socket, config);
    }

    public static void applySpotOptions(SpotNode node, Config config) {
        PerfTransport.applySpotOptions(node, config);
    }

    public static void await(CountDownLatch latch, String label, Duration timeout) {
        PerfTransport.await(latch, label, timeout);
    }

    public static void join(Thread thread, String label, Duration timeout) {
        PerfTransport.join(thread, label, timeout);
    }

    public static void waitForMonitorEvent(MonitorSocket monitor,
                                           systems.zlink.MonitorEventType expectedEvent,
                                           int expectedCount, Duration timeout,
                                           String label) {
        PerfTransport.waitForMonitorEvent(monitor, expectedEvent, expectedCount,
            timeout, label);
    }

    public static systems.zlink.Received recvNoWait(PairSocket socket) {
        return tryRecv(() -> socket.recv(RecvFlags.DONT_WAIT));
    }

    public static systems.zlink.Received recvNoWait(DealerSocket socket) {
        return tryRecv(() -> socket.recv(RecvFlags.DONT_WAIT));
    }

    public static systems.zlink.Received recvNoWait(RouterSocket socket) {
        return tryRecv(() -> socket.recv(RecvFlags.DONT_WAIT));
    }

    public static systems.zlink.Received recvNoWait(StreamSocket socket) {
        return tryRecv(() -> socket.recv(RecvFlags.DONT_WAIT));
    }

    public static Optional<TopicMessage> subscribeNoWait(SubSocket socket) {
        return tryOptional(() -> socket.subscribe(RecvFlags.DONT_WAIT));
    }

    public static Optional<TopicMessage> subscribeNoWait(Spot spot) {
        return tryOptional(() -> spot.subscribe(RecvFlags.DONT_WAIT));
    }

    public static void applyMonitorOptions(MonitorSocket monitor, Config config) {
        PerfTransport.applyMonitorOptions(monitor, config);
    }

    public static Path ensureResultsDir(Path root, String suite, String leaf) {
        return PerfReport.ensureResultsDir(root, suite, leaf);
    }

    public static String resultFileName(String lang, String suite, String platform, String tag) {
        return PerfReport.resultFileName(lang, suite, platform, tag);
    }

    public static long nowNs() {
        return PerfMeasurement.nowNs();
    }

    private static <T> Optional<T> tryOptional(CheckedSupplier<T> supplier) {
        try {
            return Optional.ofNullable(supplier.get());
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA
                || ex.getResult() == RecvResult.BUSY) {
                return Optional.empty();
            }
            throw ex;
        }
    }

    private static <T> T tryRecv(CheckedSupplier<T> supplier) {
        try {
            return supplier.get();
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA
                || ex.getResult() == RecvResult.BUSY) {
                return null;
            }
            throw ex;
        } catch (Exception ex) {
            throw new IllegalStateException("recv failed", ex);
        }
    }

    @FunctionalInterface
    private interface CheckedSupplier<T> {
        T get();
    }

    public static int intEnv(String name, int fallback) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            return fallback;
        }
        return Integer.parseInt(value);
    }
}
