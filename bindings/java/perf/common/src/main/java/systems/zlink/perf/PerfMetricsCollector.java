/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.LongAdder;

final class PerfMetricsCollector {
    // Lock-free counters: multi-client perf can call recordNanos from N
    // threads concurrently (one thread per client socket). A single
    // synchronized hot path serializes all per-message accounting and
    // throttles total throughput to a single core. LongAdder striping
    // removes that contention.
    private final LongAdder count = new LongAdder();
    private final LongAdder sum = new LongAdder();
    // Per-thread latency sample arrays. C perf stores all samples and computes
    // interpolated percentiles; Java keeps the same metric meaning while still
    // avoiding one global synchronized hot path.
    private final ThreadLocal<ThreadReservoir> threadReservoir;
    private final List<ThreadReservoir> registry =
        new ArrayList<>();
    private final Object registryLock = new Object();

    PerfMetricsCollector(String suite) {
        threadReservoir = ThreadLocal.withInitial(() -> {
            ThreadReservoir reservoir = new ThreadReservoir(
                resolveInitialLatencyCapacity(suite));
            synchronized (registryLock) {
                registry.add(reservoir);
            }
            return reservoir;
        });
    }

    void recordNanos(long value) {
        count.increment();
        sum.add(value);
        ThreadReservoir reservoir = threadReservoir.get();
        reservoir.add(value);
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
        double p95 = percentile(merged, 0.95d) / latencyDivisor;
        double p99 = percentile(merged, 0.99d) / latencyDivisor;
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
            config.size(), throughput, bandwidth, mean, p95, p99);
    }

    // Per-thread sample storage; no synchronization required because each
    // reservoir is owned by exactly one thread for its hot path lifetime.
    private static final class ThreadReservoir {
        private long[] samples;
        private int size;

        ThreadReservoir(int initialCapacity) {
            this.samples = new long[Math.max(1, initialCapacity)];
        }

        void add(long value) {
            if (size == samples.length) {
                samples = Arrays.copyOf(samples, samples.length * 2);
            }
            samples[size++] = value;
        }
    }

    private static double percentile(long[] sorted, double percentile) {
        if (sorted.length == 0) {
            return 0.0d;
        }
        if (percentile <= 0.0d) {
            return sorted[0];
        }
        if (percentile >= 1.0d) {
            return sorted[sorted.length - 1];
        }
        double pos = (sorted.length - 1) * percentile;
        int lo = (int) pos;
        int hi = lo + 1 < sorted.length ? lo + 1 : lo;
        double frac = pos - lo;
        return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
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
