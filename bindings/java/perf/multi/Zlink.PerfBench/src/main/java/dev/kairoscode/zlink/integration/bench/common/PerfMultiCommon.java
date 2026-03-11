/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.MonitorSocket;
import java.util.List;

public final class PerfMultiCommon {
    public static final int CONNECT_MONITOR_EVENTS =
        MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiCommon() {
    }

    public static int resolveClients(String pattern) {
        int fallback = PerfCommon.isStreamPattern(pattern) ? 10_000 : 100;
        return parsePositiveWithAliases(fallback, "PERF_CLIENTS",
            "PERF_MULTI_CLIENTS");
    }

    public static int resolveHwm(String pattern) {
        int fallback = PerfCommon.isStreamPattern(pattern) ? 10 : 100;
        return parsePositiveWithAliases(fallback, "PERF_HWM",
            "PERF_MULTI_HWM");
    }

    public static int resolveSndHwm(String pattern) {
        int hwm = resolveHwm(pattern);
        return parsePositiveWithAliases(hwm, "PERF_SNDHWM",
            "PERF_MULTI_SNDHWM");
    }

    public static int resolveRcvHwm(String pattern) {
        int hwm = resolveHwm(pattern);
        return parsePositiveWithAliases(hwm, "PERF_RCVHWM",
            "PERF_MULTI_RCVHWM");
    }

    public static int resolveWarmupSeconds() {
        return parseNonNegativeWithAliases(2, "PERF_WARMUP_SECONDS",
            "PERF_MULTI_WARMUP_SECONDS");
    }

    public static int resolveDurationSeconds() {
        return parsePositiveWithAliases(5, "PERF_DURATION_SECONDS",
            "PERF_MULTI_DURATION_SECONDS");
    }

    public static int resolveSettleMs() {
        return parseNonNegativeWithAliases(500, "PERF_SETTLE_MS",
            "PERF_MULTI_SETTLE_MS");
    }

    public static int resolveSndTimeoutMs() {
        return parsePositiveWithAliases(200, "PERF_SNDTIMEO_MS",
            "PERF_MULTI_SNDTIMEO_MS");
    }

    public static int resolveRcvTimeoutMs() {
        return parsePositiveWithAliases(200, "PERF_RCVTIMEO_MS",
            "PERF_MULTI_RCVTIMEO_MS");
    }

    public static int resolveConnectReadyTimeoutMs() {
        return parsePositiveWithAliases(5000, "PERF_CONNECT_READY_TIMEOUT_MS",
            "PERF_MULTI_CONNECT_READY_TIMEOUT_MS");
    }

    public static int resolveMonitorHwm() {
        return parsePositiveWithAliases(1000, "PERF_MONITOR_HWM",
            "PERF_MULTI_MONITOR_HWM");
    }

    public static int resolveConnectConcurrency() {
        return parsePositiveWithAliases(128, "PERF_CONNECT_CONCURRENCY",
            "PERF_MULTI_CONNECT_CONCURRENCY");
    }

    public static int resolveLatencySampleCap() {
        return parsePositiveWithAliases(100_000, "PERF_LATENCY_SAMPLE_CAP",
            "PERF_MULTI_LATENCY_SAMPLE_CAP");
    }

    public static int resolveServerBindPort() {
        return parseNonNegativeWithAliases(0, "PERF_SERVER_BIND_PORT",
            "PERF_MULTI_SERVER_BIND_PORT");
    }

    public static int resolveClientPollTimeoutMs() {
        return parseNonNegativeWithAliases(0, "PERF_CLIENT_POLL_TIMEOUT_MS",
            "PERF_MULTI_CLIENT_POLL_TIMEOUT_MS");
    }

    public static int resolveEffectiveClientPollTimeoutMs() {
        return Math.max(1, resolveClientPollTimeoutMs());
    }

    public static boolean waitAllConnectReady(List<MonitorSocket> monitors,
                                              int timeoutMs) {
        if (monitors == null || monitors.isEmpty()) {
            return true;
        }
        return PerfMultiClientHelpers.waitAllClientConnectReady(monitors,
            timeoutMs, false) >= monitors.size();
    }

    private static int parsePositiveWithAliases(int fallback,
                                                String... names) {
        for (String name : names) {
            int value = PerfCommon.parsePositiveEnv(name, -1);
            if (value > 0) {
                return value;
            }
        }
        return fallback;
    }

    private static int parseNonNegativeWithAliases(int fallback,
                                                   String... names) {
        for (String name : names) {
            int value = PerfCommon.parseNonNegativeEnv(name, -1);
            if (value >= 0) {
                return value;
            }
        }
        return fallback;
    }
}
