/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.ThreadLocalRandom;
import java.util.concurrent.atomic.LongAdder;

final class PerfMetricsCollector {
    private final int sampleCap;
    // Lock-free counters: multi-client perf can call recordNanos from N
    // threads concurrently (one thread per client socket). A single
    // synchronized hot path serializes all per-message accounting and
    // throttles total throughput to a single core. LongAdder striping
    // removes that contention.
    private final LongAdder count = new LongAdder();
    private final LongAdder sum = new LongAdder();
    // Per-thread latency reservoirs. Merged on finish — keeps recordNanos
    // lock-free in the hot path.
    private final ThreadLocal<ThreadReservoir> threadReservoir;
    private final List<ThreadReservoir> registry =
        new ArrayList<>();
    private final Object registryLock = new Object();

    PerfMetricsCollector(String suite) {
        sampleCap = Math.max(1, resolveInitialLatencyCapacity(suite));
        threadReservoir = ThreadLocal.withInitial(() -> {
            ThreadReservoir reservoir = new ThreadReservoir(sampleCap);
            synchronized (registryLock) {
                registry.add(reservoir);
            }
            return reservoir;
        });
    }

    void startActiveWindow() {
    }

    void recordNanos(long value) {
        count.increment();
        sum.add(value);
        ThreadReservoir reservoir = threadReservoir.get();
        reservoir.add(value);
    }

    void recordMillis(double value) {
        recordNanos(Math.round(value * 1_000_000.0d));
    }

    PerfUtil.Result finishSingle(PerfUtil.Config config) {
        return finish(config, 1_000_000.0d);
    }

    PerfUtil.Result finishMulti(PerfUtil.Config config) {
        return finish(config, 1_000_000.0d);
    }

    private PerfUtil.Result finish(PerfUtil.Config config, double latencyDivisor) {
        long totalCount = count.sum();
        if (totalCount == 0) {
            return new PerfUtil.Result("fail", "no_active_samples", config.pattern(),
                config.transport(), config.size(), 0.0d, 0.0d, 0.0d,
                0.0d, 0.0d);
        }
        // Merge per-thread reservoirs.
        int totalLen = 0;
        List<ThreadReservoir> snapshot;
        synchronized (registryLock) {
            snapshot = new ArrayList<>(registry);
        }
        for (ThreadReservoir reservoir : snapshot) {
            totalLen += reservoir.size;
        }
        long[] merged = new long[totalLen];
        int offset = 0;
        for (ThreadReservoir reservoir : snapshot) {
            System.arraycopy(reservoir.samples, 0, merged, offset, reservoir.size);
            offset += reservoir.size;
        }
        Arrays.sort(merged);
        double throughput = totalCount / (double) config.durationSeconds();
        double bandwidth = throughput * config.size()
            * (PerfMeasurement.isEchoPattern(config.pattern()) ? 2.0d : 1.0d)
            / 1_000_000.0d;
        double mean = (sum.sum() / (double) totalCount) / latencyDivisor;
        double p95 = merged.length > 0
            ? merged[index(merged.length, 0.95d)] / latencyDivisor : 0.0d;
        double p99 = merged.length > 0
            ? merged[index(merged.length, 0.99d)] / latencyDivisor : 0.0d;
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
            config.size(), throughput, bandwidth, mean, p95, p99);
    }

    // Per-thread reservoir; no synchronization required because each
    // reservoir is owned by exactly one thread for its hot path lifetime.
    private static final class ThreadReservoir {
        private final long[] samples;
        private final int cap;
        private int size;
        private long seen;

        ThreadReservoir(int cap) {
            this.cap = cap;
            this.samples = new long[cap];
        }

        void add(long value) {
            seen++;
            if (size < cap) {
                samples[size++] = value;
                return;
            }
            long slot = ThreadLocalRandom.current().nextLong(seen);
            if (slot < cap) {
                samples[(int) slot] = value;
            }
        }
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
