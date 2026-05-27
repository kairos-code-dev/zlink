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
    private static final ConcurrentMap<Key, Pump> PUMPS = new ConcurrentHashMap<>();
    private static final ConcurrentMap<Key, AtomicInteger> EXTERNAL_PROGRESS =
        new ConcurrentHashMap<>();

    private RequestProgressPump() {
    }

    public static void trackSocketRequest(CompletableFuture<?> future,
                                          MemorySegment socketHandle,
                                          String threadName) {
        track(future, socketHandle, threadName, Kind.SOCKET);
    }

    public static void trackSpotRequest(CompletableFuture<?> future,
                                        MemorySegment socketHandle,
                                        String threadName) {
        track(future, socketHandle, threadName, Kind.SPOT);
    }

    private static void track(CompletableFuture<?> future,
                              MemorySegment socketHandle,
                              String threadName,
                              Kind kind) {
        Objects.requireNonNull(future, "future");
        Objects.requireNonNull(socketHandle, "socketHandle");
        if (future.isDone() || socketHandle.address() == 0) {
            return;
        }
        Key key = new Key(kind, socketHandle.address());
        AtomicInteger external = EXTERNAL_PROGRESS.get(key);
        if (external != null && external.get() > 0) {
            return;
        }
        Pump pump = PUMPS.computeIfAbsent(key,
            ignored -> new Pump(key, socketHandle, threadName));
        pump.track(future);
    }

    public static void acquireExternalSpotProgress(MemorySegment socketHandle) {
        acquireExternalProgress(socketHandle, Kind.SPOT);
    }

    public static void releaseExternalSpotProgress(MemorySegment socketHandle) {
        releaseExternalProgress(socketHandle, Kind.SPOT);
    }

    private static void acquireExternalProgress(MemorySegment socketHandle,
                                                Kind kind) {
        Objects.requireNonNull(socketHandle, "socketHandle");
        if (socketHandle.address() == 0)
            return;
        Key key = new Key(kind, socketHandle.address());
        EXTERNAL_PROGRESS.computeIfAbsent(key, ignored -> new AtomicInteger())
            .incrementAndGet();
    }

    private static void releaseExternalProgress(MemorySegment socketHandle,
                                                Kind kind) {
        Objects.requireNonNull(socketHandle, "socketHandle");
        if (socketHandle.address() == 0)
            return;
        Key key = new Key(kind, socketHandle.address());
        AtomicInteger count = EXTERNAL_PROGRESS.get(key);
        if (count != null && count.decrementAndGet() <= 0) {
            EXTERNAL_PROGRESS.remove(key, count);
        }
    }

    private enum Kind {
        SOCKET,
        SPOT
    }

    private record Key(Kind kind, long address) {
    }

    private static final class Pump {
        private final Key key;
        private final MemorySegment socketHandle;
        private final String threadName;
        private final AtomicInteger pending = new AtomicInteger();
        private final AtomicBoolean running = new AtomicBoolean();

        private Pump(Key key, MemorySegment socketHandle, String threadName) {
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
            thread.start();
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
                if (pending.get() > 0) {
                    ensureRunning();
                    return;
                }
                PUMPS.remove(key, this);
            }
        }
    }
}
