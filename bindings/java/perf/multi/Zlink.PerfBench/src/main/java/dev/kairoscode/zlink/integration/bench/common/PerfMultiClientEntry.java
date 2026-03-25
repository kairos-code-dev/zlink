/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.integration.bench.src.PerfMultiDealerDealerClient;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiDealerRouterClient;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiPubSubClient;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiRouterRouterClient;
import dev.kairoscode.zlink.integration.bench.src.PerfMultiSpotClient;
import java.lang.management.ManagementFactory;
import com.sun.management.OperatingSystemMXBean;

public final class PerfMultiClientEntry {
    private PerfMultiClientEntry() {
    }

    public static int runClient(String pattern, String transport, int msgSize,
                                String endpoint) {
        long wallBegin = System.nanoTime();
        long cpuBegin = readProcessCpuNanos();

        int rc = switch (pattern) {
            case "MULTI_DEALER_DEALER" -> PerfMultiDealerDealerClient.runClient(
                transport, msgSize, endpoint);
            case "MULTI_DEALER_ROUTER" -> PerfMultiDealerRouterClient.runClient(
                transport, msgSize, endpoint);
            case "MULTI_ROUTER_ROUTER" -> PerfMultiRouterRouterClient.runClient(
                transport, msgSize, endpoint);
            case "MULTI_PUBSUB" -> PerfMultiPubSubClient.runClient(transport,
                msgSize, endpoint);
            case "MULTI_SPOT" -> PerfMultiSpotClient.runClient(transport, msgSize,
                endpoint);
            case "MULTI_STREAM", "MULTI_STREAM_CALLBACK", "MULTI_STREAM_LEN32BE" ->
                1;
            default -> 1;
        };

        long wallEnd = System.nanoTime();
        long cpuEnd = readProcessCpuNanos();

        if (rc == 0) {
            double cpuPct = computeCpuPct(cpuBegin, cpuEnd, wallBegin, wallEnd);
            double memMb = readProcessMemoryMb();
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",client_cpu_pct," + cpuPct);
            System.out.println("RESULT,current," + pattern + "," + transport + ","
                + msgSize + ",client_mem_mb," + memMb);
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
