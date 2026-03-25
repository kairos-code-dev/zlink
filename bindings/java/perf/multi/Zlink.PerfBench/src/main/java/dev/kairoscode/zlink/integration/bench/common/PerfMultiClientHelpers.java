/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.MonitorEvent;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.ZlinkException;
import java.util.List;

public final class PerfMultiClientHelpers {
    private PerfMultiClientHelpers() {
    }

    public static boolean isSupportedTransport(String pattern,
                                               String transport) {
        String tr = transport == null ? "" : transport.trim().toLowerCase();
        if (!(tr.equals("tcp") || tr.equals("tls") || tr.equals("ws")
            || tr.equals("wss") || tr.equals("inproc") || tr.equals("ipc"))) {
            return false;
        }

        String p = pattern == null ? "" : pattern.toUpperCase();
        if (p.equals("MULTI_SPOT")
            && (tr.equals("inproc") || tr.equals("ipc"))) {
            return false;
        }

        return true;
    }

    public static String parseEndpointArg(String[] args) {
        if (args == null) {
            return "";
        }
        for (int i = 0; i + 1 < args.length; i++) {
            if ("--endpoint".equals(args[i])) {
                return args[i + 1] == null ? "" : args[i + 1].trim();
            }
        }
        return "";
    }

    public static int waitAllClientConnectReady(List<MonitorSocket> monitors,
                                                int timeoutMs,
                                                boolean acceptFallback) {
        if (monitors == null || monitors.isEmpty()) {
            return 0;
        }

        boolean[] ready = new boolean[monitors.size()];
        int[] activeIndices = new int[monitors.size()];
        int activeCount = 0;
        int readyCount = 0;

        for (int i = 0; i < monitors.size(); i++) {
            if (monitors.get(i) == null) {
                ready[i] = true;
                readyCount++;
                continue;
            }
            activeIndices[activeCount++] = i;
        }
        if (readyCount >= monitors.size()) {
            return readyCount;
        }

        long boundedTimeoutMs = Math.max(0L, timeoutMs);
        long deadlineNs = System.nanoTime()
            + boundedTimeoutMs * 1_000_000L;
        try (SocketPollSet pollSet = new SocketPollSet(activeCount)) {
            for (int i = 0; i < activeCount; i++) {
                pollSet.setSocket(i, monitors.get(activeIndices[i]).socket(),
                    PollEventType.POLLIN.getValue());
            }

            while (readyCount < monitors.size()) {
                long remainNs = deadlineNs - System.nanoTime();
                if (remainNs <= 0L) {
                    break;
                }
                int timeoutSliceMs = (int) Math.max(0L, remainNs / 1_000_000L);
                int readyEvents = pollSet.poll(timeoutSliceMs);
                if (readyEvents <= 0) {
                    continue;
                }

                for (int i = 0; i < activeCount; i++) {
                    int monitorIndex = activeIndices[i];
                    if (ready[monitorIndex]
                        || !pollSet.isReady(i, PollEventType.POLLIN.getValue())) {
                        continue;
                    }
                    if (drainMonitorReady(monitors.get(monitorIndex),
                            acceptFallback)) {
                        ready[monitorIndex] = true;
                        readyCount++;
                    }
                }
            }
        }

        return readyCount;
    }

    public static boolean waitConnectReadyCount(MonitorSocket monitor,
                                                int expectedReady,
                                                int timeoutMs) {
        if (expectedReady <= 0) {
            return true;
        }
        if (monitor == null) {
            return false;
        }

        int readyCount = drainMonitorReadyCount(monitor, false);
        if (readyCount >= expectedReady) {
            return true;
        }

        long boundedTimeoutMs = Math.max(0L, timeoutMs);
        long deadlineNs = System.nanoTime()
            + boundedTimeoutMs * 1_000_000L;
        try (SocketPollSet pollSet = new SocketPollSet(1)) {
            pollSet.setSocket(0, monitor.socket(), PollEventType.POLLIN.getValue());

            while (readyCount < expectedReady) {
                long remainNs = deadlineNs - System.nanoTime();
                if (remainNs <= 0L) {
                    break;
                }
                int timeoutSliceMs = (int) Math.max(0L, remainNs / 1_000_000L);
                int readyEvents = pollSet.poll(timeoutSliceMs);
                if (readyEvents <= 0
                    || !pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                    continue;
                }
                readyCount += drainMonitorReadyCount(monitor, false);
            }
        }

        return readyCount >= expectedReady;
    }

    private static boolean drainMonitorReady(MonitorSocket monitor,
                                             boolean acceptFallback) {
        return drainMonitorReadyCount(monitor, acceptFallback) > 0;
    }

    private static int drainMonitorReadyCount(MonitorSocket monitor,
                                              boolean acceptFallback) {
        int readyCount = 0;
        while (true) {
            try {
                MonitorEvent event = monitor.recv(ReceiveFlag.DONTWAIT);
                long mask = event.event();
                if ((mask & dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY
                    .getValue()) != 0) {
                    readyCount++;
                    continue;
                }
                if (acceptFallback
                    && (((mask & dev.kairoscode.zlink.MonitorEventType.ACCEPTED
                        .getValue()) != 0)
                    || ((mask & dev.kairoscode.zlink.MonitorEventType.CONNECTED
                        .getValue()) != 0))) {
                    readyCount++;
                }
            } catch (ZlinkException ex) {
                if (PerfCommon.isInterrupted(ex.errno())) {
                    continue;
                }
                if (PerfCommon.isWouldBlock(ex.errno())) {
                    return readyCount;
                }
                return readyCount;
            }
        }
    }
}
