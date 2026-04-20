/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import java.util.Arrays;

final class PerfMetricsCollector {
    private long[] latencies;
    private int size;
    private long count;
    private long sum;

    PerfMetricsCollector(String suite) {
        int initialCapacity = Math.max(1, resolveInitialLatencyCapacity(suite));
        latencies = new long[Math.min(initialCapacity, 1 << 20)];
    }

    void startActiveWindow() {
    }

    synchronized void recordNanos(long value) {
        count++;
        sum += value;
        ensureCapacity();
        latencies[size++] = value;
    }

    void recordMillis(double value) {
        recordNanos(Math.round(value * 1_000_000.0d));
    }

    synchronized PerfUtil.Result finishSingle(PerfUtil.Config config) {
        return finish(config, 1_000_000.0d);
    }

    synchronized PerfUtil.Result finishMulti(PerfUtil.Config config) {
        return finish(config, 1_000_000.0d);
    }

    private PerfUtil.Result finish(PerfUtil.Config config, double latencyDivisor) {
        if (count == 0) {
            return new PerfUtil.Result("fail", "no_active_samples", config.pattern(),
                config.transport(), config.size(), 0.0d, 0.0d, 0.0d,
                0.0d, 0.0d);
        }
        long[] sorted = Arrays.copyOf(latencies, size);
        Arrays.sort(sorted);
        double throughput = count / (double) config.durationSeconds();
        double bandwidth = throughput * config.size()
            * (PerfMeasurement.isEchoPattern(config.pattern()) ? 2.0d : 1.0d)
            / 1_000_000.0d;
        double mean = (sum / (double) count) / latencyDivisor;
        double p95 = sorted[index(sorted.length, 0.95d)] / latencyDivisor;
        double p99 = sorted[index(sorted.length, 0.99d)] / latencyDivisor;
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
            config.size(), throughput, bandwidth, mean, p95, p99);
    }

    private void ensureCapacity() {
        if (size < latencies.length) {
            return;
        }
        latencies = Arrays.copyOf(latencies, latencies.length * 2);
    }

    private static int index(int length, double percentile) {
        return Math.max(0, Math.min(length - 1,
            (int) Math.ceil(length * percentile) - 1));
    }

    private static int resolveInitialLatencyCapacity(String suite) {
        String envName = "multi".equals(suite)
            ? "PERF_MULTI_LATENCY_SAMPLE_CAP"
            : "PERF_SINGLE_LATENCY_SAMPLE_CAP";
        return Math.max(1, intEnv(envName, 200_000));
    }

    private static int intEnv(String name, int fallback) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            return fallback;
        }
        return Integer.parseInt(value);
    }
}
