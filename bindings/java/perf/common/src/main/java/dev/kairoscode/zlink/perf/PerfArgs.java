/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import java.util.Locale;

final class PerfArgs {
    private PerfArgs() {
    }

    static PerfUtil.Config parseSingleArgs(String[] args) {
        if (args.length < 3) {
            throw new IllegalArgumentException("usage: <pattern> <transport> <size>");
        }
        String pattern = args[0].toUpperCase(Locale.ROOT);
        String transport = args[1].toLowerCase(Locale.ROOT);
        int size = Integer.parseInt(args[2]);
        if (size < PerfUtil.HEADER_SIZE) {
            throw new IllegalArgumentException("msg size must be >= " + PerfUtil.HEADER_SIZE);
        }
        int duration = intEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int ioThreads = intEnv("PERF_IO_THREADS", 0);
        int sendHwm = intEnv("PERF_SINGLE_SNDHWM",
            intEnv("PERF_SINGLE_HWM", 1000));
        int recvHwm = intEnv("PERF_SINGLE_RCVHWM",
            intEnv("PERF_SINGLE_HWM", 1000));
        int sendTimeoutMs = intEnv("PERF_SINGLE_SNDTIMEO_MS", 200);
        int recvTimeoutMs = intEnv("PERF_SINGLE_RCVTIMEO_MS", 200);
        int monitorHwm = 1000;
        int connectReadyTimeoutMs = intEnv("PERF_SINGLE_CONNECT_READY_TIMEOUT_MS",
            intEnv("PERF_CONNECT_READY_TIMEOUT_MS", 20_000));
        for (int i = 3; i + 1 < args.length; i += 2) {
            switch (args[i]) {
                case "--duration" -> duration = Integer.parseInt(args[i + 1]);
                case "--io-threads" -> ioThreads = Integer.parseInt(args[i + 1]);
                case "--send-hwm" -> sendHwm = Integer.parseInt(args[i + 1]);
                case "--recv-hwm" -> recvHwm = Integer.parseInt(args[i + 1]);
                case "--sndtimeo", "--send-timeout-ms" ->
                    sendTimeoutMs = Integer.parseInt(args[i + 1]);
                case "--rcvtimeo", "--recv-timeout-ms" ->
                    recvTimeoutMs = Integer.parseInt(args[i + 1]);
                case "--monitor-hwm" -> monitorHwm = Integer.parseInt(args[i + 1]);
                case "--connect-ready-timeout-ms" ->
                    connectReadyTimeoutMs = Integer.parseInt(args[i + 1]);
                default -> {
                }
            }
        }
        return new PerfUtil.Config(pattern, transport, size, duration, "",
            1, 0, ioThreads, sendHwm, recvHwm, sendTimeoutMs, recvTimeoutMs,
            monitorHwm, connectReadyTimeoutMs, 0);
    }

    static PerfUtil.Config parseMultiArgs(String[] args) {
        if (args.length < 8) {
            throw new IllegalArgumentException("usage: --multi-(server|client) ...");
        }
        String pattern = args[1].toUpperCase(Locale.ROOT);
        if (pattern.startsWith("MULTI_")) {
            pattern = pattern.substring("MULTI_".length());
        }
        String transport = args[2].toLowerCase(Locale.ROOT);
        int size = Integer.parseInt(args[3]);
        if (size < PerfUtil.HEADER_SIZE) {
            throw new IllegalArgumentException("msg size must be >= " + PerfUtil.HEADER_SIZE);
        }
        int duration = intEnv("PERF_MULTI_DURATION_SECONDS", 5);
        String endpoint = "";
        int clients = intEnv("PERF_MULTI_CLIENTS", 100);
        int controlPort = 0;
        int ioThreads = intEnv("PERF_MULTI_DEFAULT_IO_THREADS", 2);
        if ("--multi-server".equals(args[0])) {
            ioThreads = intEnv("PERF_MULTI_SERVER_IO_THREADS", ioThreads);
        } else if ("--multi-client".equals(args[0])) {
            ioThreads = intEnv("PERF_MULTI_CLIENT_IO_THREADS", ioThreads);
        }
        int sendHwm = intEnv("PERF_MULTI_SNDHWM",
            intEnv("PERF_MULTI_HWM", 1000));
        int recvHwm = intEnv("PERF_MULTI_RCVHWM",
            intEnv("PERF_MULTI_HWM", 1000));
        int sendTimeoutMs = intEnv("PERF_MULTI_SNDTIMEO_MS", 200);
        int recvTimeoutMs = intEnv("PERF_MULTI_RCVTIMEO_MS", 200);
        int monitorHwm = intEnv("PERF_MULTI_MONITOR_HWM", 1000);
        int connectReadyTimeoutMs = intEnv("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);
        int connectConcurrency = intEnv("PERF_MULTI_CONNECT_CONCURRENCY",
            clients >= 10_000 ? 1024 : 128);
        for (int i = 4; i + 1 < args.length; i += 2) {
            switch (args[i]) {
                case "--duration" -> duration = Integer.parseInt(args[i + 1]);
                case "--endpoint" -> endpoint = args[i + 1];
                case "--clients" -> clients = Integer.parseInt(args[i + 1]);
                case "--control-port" -> controlPort = Integer.parseInt(args[i + 1]);
                case "--io-threads" -> ioThreads = Integer.parseInt(args[i + 1]);
                case "--send-hwm" -> sendHwm = Integer.parseInt(args[i + 1]);
                case "--recv-hwm" -> recvHwm = Integer.parseInt(args[i + 1]);
                case "--sndtimeo", "--send-timeout-ms" ->
                    sendTimeoutMs = Integer.parseInt(args[i + 1]);
                case "--rcvtimeo", "--recv-timeout-ms" ->
                    recvTimeoutMs = Integer.parseInt(args[i + 1]);
                case "--monitor-hwm" -> monitorHwm = Integer.parseInt(args[i + 1]);
                case "--connect-ready-timeout-ms" ->
                    connectReadyTimeoutMs = Integer.parseInt(args[i + 1]);
                case "--connect-concurrency" ->
                    connectConcurrency = Integer.parseInt(args[i + 1]);
                default -> {
                }
            }
        }
        if (connectConcurrency <= 0) {
            connectConcurrency = clients >= 10_000 ? 1024 : 128;
        }
        return new PerfUtil.Config(pattern, transport, size, duration, endpoint,
            clients, controlPort, ioThreads, sendHwm, recvHwm, sendTimeoutMs,
            recvTimeoutMs, monitorHwm, connectReadyTimeoutMs,
            connectConcurrency);
    }

    private static int intEnv(String name, int fallback) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            return fallback;
        }
        return Integer.parseInt(value);
    }
}
