package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.management.ManagementFactory;
import java.lang.management.OperatingSystemMXBean;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

final class PerfMulti {
    private static final byte[] STOP_TOKEN =
      "__zlink_perf_stop__".getBytes(StandardCharsets.UTF_8);

    private PerfMulti() {
    }

    private static boolean isRouterMode(String pattern) {
        return "MULTI_DEALER_ROUTER".equalsIgnoreCase(pattern)
          || "MULTI_ROUTER_ROUTER".equalsIgnoreCase(pattern);
    }

    private static boolean isStreamPattern(String pattern) {
        return "MULTI_STREAM".equalsIgnoreCase(pattern)
          || "MULTI_STREAM_CALLBACK".equalsIgnoreCase(pattern)
          || "MULTI_STREAM_LEN32BE".equalsIgnoreCase(pattern);
    }

    private static boolean isStreamCallbackPattern(String pattern) {
        return "MULTI_STREAM_CALLBACK".equalsIgnoreCase(pattern)
          || "MULTI_STREAM_LEN32BE".equalsIgnoreCase(pattern);
    }

    private static boolean isStreamLen32bePattern(String pattern) {
        return "MULTI_STREAM_LEN32BE".equalsIgnoreCase(pattern);
    }

    private static String endpointForServer(String transport, String name) {
        int bindPort = PerfUtil.parseEnv("PERF_MULTI_SERVER_BIND_PORT", 0);
        if (bindPort > 0) {
            return transport + "://127.0.0.1:" + bindPort;
        }
        return PerfUtil.endpointFor(transport, name);
    }

    private static boolean isMonitorReady(long eventValue, boolean acceptFallback) {
        if (eventValue == MonitorEventType.CONNECTION_READY.getValue()) {
            return true;
        }
        if (!acceptFallback) {
            return false;
        }
        return eventValue == MonitorEventType.ACCEPTED.getValue()
          || eventValue == MonitorEventType.CONNECTED.getValue();
    }

    private static boolean waitMonitorReady(MonitorSocket monitor,
                                            int timeoutMs,
                                            boolean acceptFallback) {
        long deadlineNs = System.nanoTime() + Math.max(1, timeoutMs) * 1_000_000L;
        while (System.nanoTime() < deadlineNs) {
            try {
                MonitorEvent evt = monitor.recv(ReceiveFlag.DONTWAIT.getValue());
                if (isMonitorReady(evt.event(), acceptFallback)) {
                    return true;
                }
            } catch (Exception ignored) {
                PerfUtil.sleep(1);
            }
        }
        return false;
    }

    private static boolean isStreamEventPayload(byte[] payload, int payloadLen) {
        return payloadLen == 1 && (payload[0] == 0x00 || payload[0] == 0x01);
    }

    private static boolean isStreamEventPayload(ByteSpan payload) {
        return payload.length() == 1
          && (payload.segment().get(ValueLayout.JAVA_BYTE, 0) == 0x00
          || payload.segment().get(ValueLayout.JAVA_BYTE, 0) == 0x01);
    }

    private static boolean equalsStop(ByteSpan payload) {
        if (payload.length() != STOP_TOKEN.length) {
            return false;
        }
        for (int i = 0; i < STOP_TOKEN.length; i++) {
            if (payload.segment().get(ValueLayout.JAVA_BYTE, i) != STOP_TOKEN[i]) {
                return false;
            }
        }
        return true;
    }

    private static final class Len32StopTokenParser {
        private byte[] buffer = new byte[2048];
        private int start = 0;
        private int end = 0;

        boolean consume(ByteSpan payload) {
            int len = payload.length();
            if (len <= 0) {
                return false;
            }
            ensureCapacity(len);
            MemorySegment.copy(payload.segment(), 0, MemorySegment.ofArray(buffer), end, len);
            end += len;

            boolean foundStop = false;
            while ((end - start) >= 4) {
                int bodyLen = ((buffer[start] & 0xFF) << 24)
                  | ((buffer[start + 1] & 0xFF) << 16)
                  | ((buffer[start + 2] & 0xFF) << 8)
                  | (buffer[start + 3] & 0xFF);
                if (bodyLen < 0 || bodyLen > (8 * 1024 * 1024)) {
                    start = 0;
                    end = 0;
                    break;
                }
                int frameLen = 4 + bodyLen;
                if ((end - start) < frameLen) {
                    break;
                }
                if (bodyLen == STOP_TOKEN.length) {
                    boolean matches = true;
                    for (int i = 0; i < STOP_TOKEN.length; i++) {
                        if (buffer[start + 4 + i] != STOP_TOKEN[i]) {
                            matches = false;
                            break;
                        }
                    }
                    if (matches) {
                        foundStop = true;
                    }
                }
                start += frameLen;
            }
            compact();
            return foundStop;
        }

        private void ensureCapacity(int incoming) {
            int required = end + incoming;
            if (required <= buffer.length) {
                return;
            }
            compact();
            required = end + incoming;
            if (required <= buffer.length) {
                return;
            }
            int next = buffer.length;
            while (next < required) {
                next *= 2;
            }
            byte[] grown = new byte[next];
            int remain = end - start;
            if (remain > 0) {
                System.arraycopy(buffer, start, grown, 0, remain);
            }
            buffer = grown;
            start = 0;
            end = remain;
        }

        private void compact() {
            if (start <= 0) {
                return;
            }
            if (start >= end) {
                start = 0;
                end = 0;
                return;
            }
            int remain = end - start;
            System.arraycopy(buffer, start, buffer, 0, remain);
            start = 0;
            end = remain;
        }
    }

    private static int runStreamCallbackServerLoop(
      Socket server,
      String pattern,
      String endpoint
    ) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        AtomicBoolean callbackFailed = new AtomicBoolean(false);
        Len32StopTokenParser stopParser = new Len32StopTokenParser();
        StreamDispatchMode mode = isStreamLen32bePattern(pattern)
          ? StreamDispatchMode.LEN32BE
          : StreamDispatchMode.NONE;

        server.attachStream((rid, payload) -> {
            if (payload.length() <= 0 || isStreamEventPayload(payload)) {
                return 0;
            }
            if (equalsStop(payload)) {
                stopRequested.set(true);
                return 0;
            }
            try {
                server.streamSend(rid, payload);
            } catch (Exception ex) {
                System.err.println("PerfMulti stream callback send failed: " + ex.getMessage());
                callbackFailed.set(true);
                stopRequested.set(true);
            }
            if (isStreamLen32bePattern(pattern)) {
                if (equalsStop(payload)) {
                    stopRequested.set(true);
                }
            } else if (stopParser.consume(payload)) {
                stopRequested.set(true);
            }
            return 0;
        }, mode);

        try {
            // Advertise READY only after callback dispatch is fully attached
            // to avoid a client/server startup race.
            System.out.println("READY," + endpoint);
            while (!stopRequested.get()) {
                if (callbackFailed.get()) {
                    return 2;
                }
                PerfUtil.sleep(2);
            }
        } finally {
            try {
                server.detachStream();
            } catch (Exception ignored) {
            }
        }
        return callbackFailed.get() ? 2 : 0;
    }

    static int runServer(String pattern, String transport, int size) {
        size = Math.max(1, size);
        int rcvTimeoutMs = PerfUtil.parseEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        int readyTimeoutMs = PerfUtil.parseEnv(
          "PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);
        String endpoint = endpointForServer(transport, "multi-" + pattern);

        long wallStart = System.nanoTime();
        long cpuStart = currentProcessCpuNanos();

        Context ctx = new Context();
        try {
            if (isStreamPattern(pattern)) {
                Socket server = new Socket(ctx, SocketType.STREAM);
                try {
                    // Keep blocking recv loop for raw STREAM server path.
                    server.setSockOpt(SocketOption.RCVTIMEO, -1);
                    server.bind(endpoint);

                    if (isStreamCallbackPattern(pattern)) {
                        int rc = runStreamCallbackServerLoop(server, pattern, endpoint);
                        if (rc != 0) {
                            return rc;
                        }
                    } else {
                        int rc = runStreamCallbackServerLoop(server, pattern, endpoint);
                        if (rc != 0) {
                            return rc;
                        }
                    }
                } finally {
                    try {
                        server.close();
                    } catch (Exception ignored) {
                    }
                }
            } else if (isRouterMode(pattern)) {
                Socket server = new Socket(ctx, SocketType.ROUTER);
                try {
                    MonitorSocket monitor = server.monitorOpen(
                      MonitorEventType.combine(
                        MonitorEventType.CONNECTION_READY,
                        MonitorEventType.ACCEPTED,
                        MonitorEventType.CONNECTED));
                    server.setSockOpt(SocketOption.RCVTIMEO, rcvTimeoutMs);
                    server.bind(endpoint);
                    System.out.println("READY," + endpoint);
                    if (!waitMonitorReady(monitor, readyTimeoutMs, true)) {
                        monitor.close();
                        return 2;
                    }
                    monitor.close();

                    int cap = Math.max(256, Math.max(size, STOP_TOKEN.length));
                    byte[] ridBuf = new byte[256];
                    byte[] payloadBuf = new byte[cap];
                    while (true) {
                        int ridLen = server.recv(ridBuf, ReceiveFlag.NONE);
                        int payloadLen = server.recv(payloadBuf, ReceiveFlag.NONE);
                        if (equalsStop(payloadBuf, payloadLen)) {
                            break;
                        }
                        server.send(ridBuf, 0, ridLen, SendFlag.SNDMORE);
                        server.send(payloadBuf, 0, payloadLen, SendFlag.NONE);
                    }
                } finally {
                    try {
                        server.close();
                    } catch (Exception ignored) {
                    }
                }
            } else {
                Socket server = new Socket(ctx, SocketType.DEALER);
                try {
                    MonitorSocket monitor = server.monitorOpen(
                      MonitorEventType.combine(
                        MonitorEventType.CONNECTION_READY,
                        MonitorEventType.ACCEPTED,
                        MonitorEventType.CONNECTED));
                    server.setSockOpt(SocketOption.RCVTIMEO, rcvTimeoutMs);
                    server.bind(endpoint);
                    System.out.println("READY," + endpoint);
                    if (!waitMonitorReady(monitor, readyTimeoutMs, true)) {
                        monitor.close();
                        return 2;
                    }
                    monitor.close();

                    int cap = Math.max(256, Math.max(size, STOP_TOKEN.length));
                    byte[] payloadBuf = new byte[cap];
                    while (true) {
                        int payloadLen = server.recv(payloadBuf, ReceiveFlag.NONE);
                        if (equalsStop(payloadBuf, payloadLen)) {
                            break;
                        }
                        server.send(payloadBuf, 0, payloadLen, SendFlag.NONE);
                    }
                } finally {
                    try {
                        server.close();
                    } catch (Exception ignored) {
                    }
                }
            }

            long wallEnd = System.nanoTime();
            long cpuEnd = currentProcessCpuNanos();
            double wallSec = Math.max(1e-9, (wallEnd - wallStart) / 1_000_000_000.0);
            double cpuSec = Math.max(0.0, (cpuEnd - cpuStart) / 1_000_000_000.0);
            double ncpu = Math.max(1, Runtime.getRuntime().availableProcessors());
            double cpuPct = (cpuSec / (wallSec * ncpu)) * 100.0;
            double memMb = Runtime.getRuntime().totalMemory() / (1024.0 * 1024.0);

            System.out.println("RESULT,current," + pattern + "," + transport + ","
              + size + ",server_cpu_pct," + cpuPct);
            System.out.println("RESULT,current," + pattern + "," + transport + ","
              + size + ",server_mem_mb," + memMb);
            return 0;
        } catch (Exception e) {
            System.err.println(
              "PerfMulti.runServer failed: pattern=" + pattern
                + " transport=" + transport + " size=" + size + " error=" + e.getMessage());
            return 2;
        } finally {
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }

    static int runClient(String pattern, String transport, int size,
                         String endpoint) {
        size = Math.max(1, size);
        int warmupSeconds = PerfUtil.parseEnv("PERF_MULTI_WARMUP_SECONDS", 3);
        int durationSeconds = PerfUtil.parseEnv("PERF_MULTI_DURATION_SECONDS", 5);
        int latCount = Math.max(1, PerfUtil.parseEnv("PERF_LAT_COUNT", 200));
        int sndTimeoutMs = PerfUtil.parseEnv("PERF_MULTI_SNDTIMEO_MS", 5000);
        int rcvTimeoutMs = PerfUtil.parseEnv("PERF_MULTI_RCVTIMEO_MS", 5000);
        int readyTimeoutMs = PerfUtil.parseEnv(
          "PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);

        Context ctx = new Context();
        Socket client = new Socket(ctx, SocketType.DEALER);
        MonitorSocket monitor = client.monitorOpen(
          MonitorEventType.combine(
            MonitorEventType.CONNECTION_READY,
            MonitorEventType.CONNECTED));
        try {
            client.setSockOpt(SocketOption.SNDTIMEO, sndTimeoutMs);
            client.setSockOpt(SocketOption.RCVTIMEO, rcvTimeoutMs);
            client.connect(endpoint);
            if (!waitMonitorReady(monitor, readyTimeoutMs, false)) {
                return 2;
            }
            monitor.close();

            byte[] payload = new byte[size];
            for (int i = 0; i < payload.length; i++) {
                payload[i] = 'a';
            }
            byte[] recvBuf = new byte[Math.max(256, Math.max(size, STOP_TOKEN.length))];

            long warmupDeadline = System.nanoTime()
              + Math.max(0, warmupSeconds) * 1_000_000_000L;
            while (System.nanoTime() < warmupDeadline) {
                client.send(payload, SendFlag.NONE);
                client.recv(recvBuf, ReceiveFlag.NONE);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                client.send(payload, SendFlag.NONE);
                client.recv(recvBuf, ReceiveFlag.NONE);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            int ops = 0;
            t0 = System.nanoTime();
            long benchDeadline = System.nanoTime()
              + Math.max(1, durationSeconds) * 1_000_000_000L;
            while (System.nanoTime() < benchDeadline) {
                client.send(payload, SendFlag.NONE);
                client.recv(recvBuf, ReceiveFlag.NONE);
                ops++;
            }
            double elapsedSec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = elapsedSec > 0.0 ? ops / elapsedSec : 0.0;

            try {
                client.send(STOP_TOKEN, SendFlag.NONE);
            } catch (Exception ignored) {
            }

            PerfUtil.printResult(pattern, transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            System.err.println(
              "PerfMulti.runClient failed: pattern=" + pattern
                + " transport=" + transport + " size=" + size + " error=" + e.getMessage());
            return 2;
        } finally {
            try {
                monitor.close();
            } catch (Exception ignored) {
            }
            try {
                client.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }

    private static boolean equalsStop(byte[] payload, int payloadLen) {
        if (payloadLen != STOP_TOKEN.length) {
            return false;
        }
        for (int i = 0; i < STOP_TOKEN.length; i++) {
            if (payload[i] != STOP_TOKEN[i]) {
                return false;
            }
        }
        return true;
    }

    private static long currentProcessCpuNanos() {
        try {
            OperatingSystemMXBean osBean = ManagementFactory.getOperatingSystemMXBean();
            if (osBean instanceof com.sun.management.OperatingSystemMXBean) {
                long n = ((com.sun.management.OperatingSystemMXBean) osBean).getProcessCpuTime();
                if (n > 0) {
                    return n;
                }
            }
        } catch (Exception ignored) {
        }
        return System.nanoTime();
    }
}
