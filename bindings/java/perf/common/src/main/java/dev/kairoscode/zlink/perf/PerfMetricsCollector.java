/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import com.sun.management.OperatingSystemMXBean;
import java.lang.management.ManagementFactory;
import java.util.Arrays;

final class PerfMetricsCollector {
    private static final int MAX_LATENCY_SAMPLES = 4 * 1024 * 1024;
    private static final OperatingSystemMXBean OS_BEAN =
        ManagementFactory.getOperatingSystemMXBean()
            instanceof OperatingSystemMXBean bean ? bean : null;

    private long[] latencies = new long[Math.min(MAX_LATENCY_SAMPLES, 1 << 20)];
    private int size;
    private long count;
    private long sum;
    private long sampleStride = 1L;
    private long cpuStartNanos;
    private long wallStartNanos;
    private long memStartBytes;

    void startResourceWindow() {
        wallStartNanos = System.nanoTime();
        cpuStartNanos = OS_BEAN == null ? 0L : OS_BEAN.getProcessCpuTime();
        memStartBytes = usedMemoryBytes();
    }

    synchronized void recordMicros(long value) {
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

    void recordMillis(double value) {
        recordMicros(Math.round(value * 1000.0d));
    }

    synchronized PerfUtil.Result finishSingle(PerfUtil.Config config) {
        return finish(config, 1.0d);
    }

    synchronized PerfUtil.Result finishMulti(PerfUtil.Config config) {
        return finish(config, 1000.0d);
    }

    private PerfUtil.Result finish(PerfUtil.Config config, double latencyDivisor) {
        if (count == 0) {
            return new PerfUtil.Result("fail", "no_active_samples", config.pattern(),
                config.transport(), config.size(), 0.0d, 0.0d, 0.0d,
                0.0d, 0.0d, Double.NaN, PerfMeasurement.bytesToMb(usedMemoryBytes()));
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
        long wallDelta = Math.max(1L, System.nanoTime() - wallStartNanos);
        long cpuDelta = OS_BEAN == null ? 0L
            : Math.max(0L, OS_BEAN.getProcessCpuTime() - cpuStartNanos);
        double cpuPct = OS_BEAN == null ? Double.NaN
            : 100.0d * cpuDelta
                / (wallDelta * Runtime.getRuntime().availableProcessors());
        double memMb = PerfMeasurement.bytesToMb(Math.max(memStartBytes, usedMemoryBytes()));
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
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

    private static int index(int length, double percentile) {
        return Math.max(0, Math.min(length - 1,
            (int) Math.ceil(length * percentile) - 1));
    }

    private static long usedMemoryBytes() {
        Runtime runtime = Runtime.getRuntime();
        return runtime.totalMemory() - runtime.freeMemory();
    }
}
