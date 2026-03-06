/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.MonitorSocket;
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
        if ((p.equals("MULTI_GATEWAY") || p.equals("MULTI_SPOT"))
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
        long deadlineNs = System.nanoTime()
            + (long) Math.max(1, timeoutMs) * 1_000_000L;
        int ready = 0;
        for (MonitorSocket monitor : monitors) {
            if (monitor == null) {
                continue;
            }
            int remainMs = (int) Math.max(1L,
                (deadlineNs - System.nanoTime()) / 1_000_000L);
            if (System.nanoTime() >= deadlineNs) {
                break;
            }
            if (PerfCommon.waitMonitorReady(monitor, remainMs,
                acceptFallback)) {
                ready++;
            }
        }
        return ready;
    }
}
