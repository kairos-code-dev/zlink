/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.perf.PerfUtil;

final class SingleSendLoops {
    private SingleSendLoops() {
    }

    static Thread oneWaySend(Runnable warmupSend, Runnable activeSend,
                             Runnable stopSend, int warmupSeconds,
                             int durationSeconds, PerfUtil.Metrics metrics) {
        return new Thread(() -> {
            long warmupEnd = System.nanoTime() + warmupSeconds * 1_000_000_000L;
            while (System.nanoTime() < warmupEnd) {
                warmupSend.run();
            }
            metrics.startResourceWindow();
            long activeEnd = System.nanoTime() + durationSeconds * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                activeSend.run();
            }
            stopSend.run();
        }, "single-perf-sender");
    }
}
