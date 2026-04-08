/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Locale;

final class PerfReport {
    private static final DateTimeFormatter FILE_TS =
        DateTimeFormatter.ofPattern("yyyyMMdd_HHmmss");

    private PerfReport() {
    }

    static String format(PerfUtil.Result result) {
        if ("unsupported".equals(result.status)) {
            return String.format(Locale.ROOT, "UNSUPPORTED,java,%s,%s,%s",
                result.pattern, result.transport, sanitize(result.reason));
        }
        if (!"ok".equals(result.status)) {
            return String.format(Locale.ROOT, "FAIL,java,%s,%s,%s,%s",
                result.pattern, result.transport, result.size, sanitize(result.reason));
        }
        String key = String.format(Locale.ROOT, "RESULT,current,%s,%s,%d",
            result.pattern, result.transport, result.size);
        return String.join(System.lineSeparator(),
            metricLine(key, "throughput", result.throughput),
            metricLine(key, "bandwidth", result.bandwidth),
            metricLine(key, "latency", result.latencyMean),
            metricLine(key, "latency_p95", result.latencyP95),
            metricLine(key, "latency_p99", result.latencyP99));
    }

    static Path ensureResultsDir(Path root, String suite, String leaf) {
        Path dir = root.resolve(suite).resolve(leaf);
        try {
            Files.createDirectories(dir);
        } catch (java.io.IOException ex) {
            throw new IllegalStateException("failed to create results dir: " + dir, ex);
        }
        return dir;
    }

    static String resultFileName(String platform, String recvMode, String tag) {
        String base = "perf_" + platform + "_" + recvMode + "_"
            + LocalDateTime.now().format(FILE_TS);
        return tag == null || tag.isBlank() ? base + ".txt" : base + "_" + tag + ".txt";
    }

    private static String metricLine(String key, String metric, double value) {
        return String.format(Locale.ROOT, "%s,%s,%s", key, metric, metric(value));
    }

    private static String metric(double value) {
        return Double.isNaN(value) ? "N/A" : String.format(Locale.ROOT, "%.3f", value);
    }

    private static String sanitize(String value) {
        return value == null || value.isBlank() ? "-" : value.replace(' ', '_');
    }
}
