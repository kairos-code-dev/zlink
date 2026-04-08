/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import java.time.Duration;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.atomic.AtomicReference;

public final class PerfCallbackMetrics {
    private static final long STOP_SENTINEL = Long.MIN_VALUE;
    private static final int QUEUE_CAPACITY = 65_536;

    private final PerfUtil.Metrics metrics = new PerfUtil.Metrics();
    private final ArrayBlockingQueue<Long> queue = new ArrayBlockingQueue<>(QUEUE_CAPACITY);
    private final AtomicReference<Throwable> failure = new AtomicReference<>();
    private final Thread worker;

    PerfCallbackMetrics(String workerName) {
        worker = new Thread(this::drainLoop, workerName);
        worker.setDaemon(true);
        worker.start();
    }

    public void startActiveWindow() {
        metrics.startActiveWindow();
    }

    public void recordMicros(long value) {
        checkFailure();
        if (!queue.offer(value)) {
            throw new IllegalStateException("callback metrics queue overflow");
        }
    }

    public void recordMillis(double value) {
        recordMicros(Math.round(value * 1000.0d));
    }

    public PerfUtil.Result finishSingle(PerfUtil.Config config) {
        stopWorker();
        return metrics.finishSingle(config);
    }

    public PerfUtil.Result finishMulti(PerfUtil.Config config) {
        stopWorker();
        return metrics.finishMulti(config);
    }

    private void stopWorker() {
        if (!queue.offer(STOP_SENTINEL)) {
            throw new IllegalStateException("failed to stop callback metrics worker");
        }
        PerfUtil.join(worker, worker.getName(), Duration.ofSeconds(10));
        checkFailure();
    }

    private void drainLoop() {
        try {
            while (true) {
                long value = queue.take();
                if (value == STOP_SENTINEL) {
                    return;
                }
                metrics.recordMicros(value);
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            failure.compareAndSet(null, ex);
        } catch (Throwable error) {
            failure.compareAndSet(null, error);
        }
    }

    private void checkFailure() {
        Throwable error = failure.get();
        if (error == null) {
            return;
        }
        throw new IllegalStateException("callback metrics worker failed", error);
    }
}
