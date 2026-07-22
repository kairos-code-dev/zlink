/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.nativeapi;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Shared request-progress pumps keyed by socket handle.
 *
 * <p>This avoids a dedicated progress thread per request while keeping the same
 * public async request contract.
 */
public final class RequestProgressPump {
    private static final int ZLINK_POLLCOMPLETION = 32;
    private static final long POLLER_EVENT_SIZE = 48;
    private static final int POLLER_EVENT_BATCH = 64;
    private static final long IDLE_KEEPALIVE_NANOS = 1_000_000_000L;
    private static final int POLL_RECHECK_TIMEOUT_MS = 10;
    private static final ConcurrentMap<Long, Pump> PUMPS =
        new ConcurrentHashMap<>();

    private RequestProgressPump() {
    }

    public static void trackSocketRequest(CompletableFuture<?> future,
                                          MemorySegment socketHandle,
                                          String threadName) {
        track(future, socketHandle, threadName);
    }

    public static void stopSocketProgress(MemorySegment socketHandle) {
        stopProgress(socketHandle);
    }

    private static void track(CompletableFuture<?> future,
                              MemorySegment handle,
                              String threadName) {
        Objects.requireNonNull(future, "future");
        Objects.requireNonNull(handle, "handle");
        if (future.isDone() || handle.address() == 0) {
            return;
        }
        long key = handle.address();
        Pump pump = PUMPS.computeIfAbsent(key,
            ignored -> new Pump(key, handle, threadName));
        pump.track(future);
    }

    private static void stopProgress(MemorySegment socketHandle) {
        Objects.requireNonNull(socketHandle, "socketHandle");
        if (socketHandle.address() == 0)
            return;
        long key = socketHandle.address();
        Pump pump = PUMPS.remove(key);
        if (pump != null) {
            pump.stopAndWait();
        }
    }

    private static final class Pump {
        private final long key;
        private final MemorySegment socketHandle;
        private final String threadName;
        private final AtomicInteger pending = new AtomicInteger();
        private final AtomicBoolean running = new AtomicBoolean();
        private final AtomicBoolean stopping = new AtomicBoolean();
        private volatile Thread thread;

        private Pump(long key, MemorySegment socketHandle, String threadName) {
            this.key = key;
            this.socketHandle = socketHandle;
            this.threadName = threadName;
        }

        private void track(CompletableFuture<?> future) {
            if (future.isDone()) {
                return;
            }
            pending.incrementAndGet();
            future.whenComplete((ignored, error) -> pending.decrementAndGet());
            ensureRunning();
        }

        private void ensureRunning() {
            if (!running.compareAndSet(false, true)) {
                return;
            }
            Thread thread = new Thread(this::runLoop, threadName);
            thread.setDaemon(true);
            this.thread = thread;
            thread.start();
        }

        private void stopAndWait() {
            stopping.set(true);
            Thread current = thread;
            if (current == null || current == Thread.currentThread()) {
                return;
            }
            try {
                current.join(100);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }

        private void runLoop() {
            MemorySegment poller = MemorySegment.NULL;
            try {
                poller = Native.pollerNew();
                if (poller.address() == 0)
                    return;
                if (Native.pollerAdd(poller, socketHandle, MemorySegment.NULL,
                    ZLINK_POLLCOMPLETION) != 0) {
                    return;
                }
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment events = arena.allocate(
                        POLLER_EVENT_SIZE * POLLER_EVENT_BATCH, Long.BYTES);
                    long idleDeadlineNs = 0L;
                    for (;;) {
                        if (stopping.get()) {
                            break;
                        }
                        int timeoutMs = POLL_RECHECK_TIMEOUT_MS;
                        if (pending.get() <= 0) {
                            long now = System.nanoTime();
                            if (idleDeadlineNs == 0L) {
                                idleDeadlineNs = now + IDLE_KEEPALIVE_NANOS;
                            } else if (now >= idleDeadlineNs) {
                                break;
                            }
                        } else {
                            idleDeadlineNs = 0L;
                        }
                        try {
                            Native.pollerWait(poller, events,
                                POLLER_EVENT_BATCH, timeoutMs);
                        } catch (Throwable ignored) {
                            break;
                        }
                    }
                }
            } finally {
                if (poller.address() != 0) {
                    try {
                        Native.pollerRemove(poller, socketHandle);
                    } catch (Throwable ignored) {
                    }
                    try {
                        Native.pollerDestroy(poller);
                    } catch (Throwable ignored) {
                    }
                }
                running.set(false);
                if (!stopping.get() && pending.get() > 0) {
                    ensureRunning();
                    return;
                }
                PUMPS.remove(key, this);
                thread = null;
            }
        }

    }
}
