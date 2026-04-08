/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

final class MultiSendLoops {
    private MultiSendLoops() {
    }

    static void runClients(int clientCount, ClientFactory factory,
                           int durationSeconds) {
        List<Thread> threads = new ArrayList<>(clientCount);
        AtomicReference<Throwable> failure = new AtomicReference<>();
        for (int i = 0; i < clientCount; i++) {
            Thread thread = factory.create(i, durationSeconds);
            thread.setUncaughtExceptionHandler((current, error) ->
                failure.compareAndSet(null, error));
            threads.add(thread);
            thread.start();
        }
        for (int i = 0; i < threads.size(); i++) {
            PerfUtil.join(threads.get(i), "multi client " + i, Duration.ofSeconds(
                durationSeconds + 20L));
        }
        Throwable error = failure.get();
        if (error != null) {
            throw new IllegalStateException("multi client failed", error);
        }
    }

    interface ClientFactory {
        Thread create(int index, int durationSeconds);
    }
}
