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

    static String format(PerfUtil.Result result, String libTag) {
        if ("unsupported".equals(result.status)) {
            return String.format(Locale.ROOT, "UNSUPPORTED,%s,%s,%s",
                libTag, result.pattern, result.transport);
        }
        if (!"ok".equals(result.status)) {
            return "";
        }
        String key = String.format(Locale.ROOT, "RESULT,%s,%s,%s,%d",
            libTag, result.pattern, result.transport, result.size);
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

    static String resultFileName(String lang, String suite, String platform, String tag) {
        String base = "perf_" + lang + "_" + suite + "_" + platform + "_"
            + LocalDateTime.now().format(FILE_TS);
        return tag == null || tag.isBlank() ? base + ".txt" : base + "_" + tag + ".txt";
    }

    private static String metricLine(String key, String metric, double value) {
        return String.format(Locale.ROOT, "%s,%s,%s", key, metric, metric(value));
    }

    private static String metric(double value) {
        return Double.isNaN(value) ? "N/A" : String.format(Locale.ROOT, "%.3f", value);
    }
}
