/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

final class SingleSendLoops {
    private SingleSendLoops() {
    }

    static Thread oneWaySend(Runnable activeSend, Runnable stopSend,
                             int durationSeconds, Runnable startActiveWindow) {
        return new Thread(() -> {
            startActiveWindow.run();
            long activeEnd = System.nanoTime() + durationSeconds * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                activeSend.run();
            }
            stopSend.run();
        }, "single-perf-sender");
    }
}
