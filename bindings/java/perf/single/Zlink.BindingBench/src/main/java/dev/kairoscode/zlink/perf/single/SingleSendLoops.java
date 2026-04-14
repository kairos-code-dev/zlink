/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.ZlinkException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class SingleSendLoops {
    @FunctionalInterface
    interface SendAction {
        void run();
    }

    private SingleSendLoops() {
    }

    static Thread oneWaySend(SendAction activeSend, SendAction stopSend,
                             int durationSeconds, Runnable startActiveWindow,
                             AtomicReference<Throwable> failure,
                             CountDownLatch finished) {
        return new Thread(() -> {
            try {
                startActiveWindow.run();
                long activeEnd = System.nanoTime() + durationSeconds * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    runWithRetry(activeSend);
                }
                runWithRetry(stopSend);
            } catch (Throwable ex) {
                if (failure != null) {
                    failure.compareAndSet(null, ex);
                }
                if (finished != null) {
                    finished.countDown();
                }
            }
        }, "single-perf-sender");
    }

    static void runWithRetry(SendAction action) {
        long deadline = System.nanoTime() + 2_000_000_000L;
        int attempts = 0;
        while (true) {
            try {
                action.run();
                return;
            } catch (ZlinkException ex) {
                if (!isRetriable(ex) || System.nanoTime() >= deadline) {
                    throw ex;
                }
                attempts++;
                backoff(attempts);
            }
        }
    }

    private static boolean isRetriable(ZlinkException ex) {
        int errno = ex.getInternalErrno();
        return errno == 11 || errno == 4;
    }

    private static void backoff(int attempts) {
        if (attempts < 32) {
            Thread.onSpinWait();
            return;
        }
        try {
            Thread.sleep(1L);
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("sender retry interrupted", ex);
        }
    }
}
