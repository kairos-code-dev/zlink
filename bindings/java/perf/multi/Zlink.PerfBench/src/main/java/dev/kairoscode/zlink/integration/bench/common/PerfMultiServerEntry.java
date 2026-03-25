/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.integration.bench.src.PerfMultiDealerDealerServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiDealerRouterServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiPubSubServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiRouterRouterServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiSpotServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiStreamCallbackServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiStreamLen32BeServer;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiStreamServer;
import java.lang.management.ManagementFactory;
import com.sun.management.OperatingSystemMXBean;

public final class PerfMultiServerEntry {
    private PerfMultiServerEntry() {
    }

    public static int runServer(String pattern, String transport, int msgSize) {
        long wallBegin = System.nanoTime();
        long cpuBegin = readProcessCpuNanos();

        int rc = switch (pattern) {
            case "MULTI_DEALER_DEALER" -> PerfMultiDealerDealerServer.runServer(
                transport, msgSize);
            case "MULTI_DEALER_ROUTER" -> PerfMultiDealerRouterServer.runServer(
                transport, msgSize);
            case "MULTI_ROUTER_ROUTER" -> PerfMultiRouterRouterServer.runServer(
                transport, msgSize);
            case "MULTI_PUBSUB" -> PerfMultiPubSubServer.runServer(transport,
                msgSize);
            case "MULTI_SPOT" -> PerfMultiSpotServer.runServer(transport, msgSize);
            case "MULTI_STREAM" -> PerfMultiStreamServer.runServer(transport,
                msgSize);
            case "MULTI_STREAM_CALLBACK" ->
                PerfMultiStreamCallbackServer.runServer(transport, msgSize);
            case "MULTI_STREAM_LEN32BE" ->
                PerfMultiStreamLen32BeServer.runServer(transport, msgSize);
            default -> 1;
        };

        long wallEnd = System.nanoTime();
        long cpuEnd = readProcessCpuNanos();

        if (rc == 0) {
            double cpuPct = computeCpuPct(cpuBegin, cpuEnd, wallBegin, wallEnd);
            double memMb = readProcessMemoryMb();
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",server_cpu_pct," + cpuPct);
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",server_mem_mb," + memMb);
            if (PerfCommon.isStreamPattern(pattern)) {
                return rc;
            }
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",server_snd_pending_max,0");
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",server_rcv_pending_max,0");
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",server_rcv_pending_end,0");
        }

        return rc;
    }

    private static long readProcessCpuNanos() {
        try {
            OperatingSystemMXBean osBean =
                (OperatingSystemMXBean) ManagementFactory.getOperatingSystemMXBean();
            return osBean.getProcessCpuTime();
        } catch (RuntimeException ex) {
            return 0L;
        }
    }

    private static double readProcessMemoryMb() {
        Runtime runtime = Runtime.getRuntime();
        long used = runtime.totalMemory() - runtime.freeMemory();
        return used / (1024.0 * 1024.0);
    }

    private static double computeCpuPct(long cpuBegin, long cpuEnd,
                                        long wallBegin, long wallEnd) {
        long cpuDelta = Math.max(0L, cpuEnd - cpuBegin);
        long wallDelta = Math.max(1L, wallEnd - wallBegin);
        double ncpu = Math.max(1, Runtime.getRuntime().availableProcessors());
        return (cpuDelta / (double) wallDelta) * 100.0 / ncpu;
    }
}
