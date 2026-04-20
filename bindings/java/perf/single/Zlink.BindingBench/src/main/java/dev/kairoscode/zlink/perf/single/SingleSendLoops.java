/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.ZlinkException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

final class SingleSendLoops {
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int ERRNO_ETIMEDOUT = 110;
    private static final int ERRNO_ETIMEDOUT_WIN = 10060;

    @FunctionalInterface
    interface SendAction {
        void run();
    }

    private SingleSendLoops() {
    }

    static Thread oneWaySend(SendAction warmupSend, SendAction activeSend,
                             SendAction stopSend, int warmupSeconds,
                             int durationSeconds, Runnable startActiveWindow,
                             AtomicReference<Throwable> failure,
                             CountDownLatch finished) {
        return new Thread(() -> {
            try {
                long warmupEnd = System.nanoTime() + warmupSeconds * 1_000_000_000L;
                while (System.nanoTime() < warmupEnd) {
                    runWithRetry(warmupSend);
                }
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
        int attempts = 0;
        while (true) {
            try {
                action.run();
                return;
            } catch (ZlinkException ex) {
                if (!isRetriable(ex)) {
                    throw ex;
                }
                attempts++;
                backoff(attempts);
            }
        }
    }

    private static boolean isRetriable(ZlinkException ex) {
        if (ex instanceof SubmitException submit
            && submit.getResult() == SubmitResult.BACKPRESSURED) {
            return true;
        }
        int errno = ex.getInternalErrno();
        return errno == ERRNO_EAGAIN
            || errno == ERRNO_EWOULDBLOCK_WIN
            || errno == ERRNO_ETIMEDOUT
            || errno == ERRNO_ETIMEDOUT_WIN
            || errno == 4;
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
