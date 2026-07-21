package systems.zlink.framework.runtime.messaging;

import java.time.Duration;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.WeakHashMap;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import java.util.logging.Level;
import java.util.logging.Logger;
import systems.zlink.framework.channels.ZLinkSubmitResult;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdmissionKey;
import systems.zlink.framework.runtime.backend.ZLinkBackendObject;

/** Shared bounded admission wait used by all one-way Java framework families. */
final class ZLinkAdmissionRuntime {
    private static final Logger LOGGER =
        Logger.getLogger(ZLinkAdmissionRuntime.class.getName());
    private static final Map<ZLinkBackendObject, Source> SOURCES = new WeakHashMap<>();
    private static final ScheduledExecutorService DEADLINES =
        Executors.newSingleThreadScheduledExecutor(runnable -> {
            Thread thread = new Thread(runnable, "zlink-java-submit-deadline");
            thread.setDaemon(true);
            return thread;
        });

    private ZLinkAdmissionRuntime() {
    }

    static CompletionStage<ZLinkSubmitResult> submit(
        ZLinkBackendObject backend,
        ZLinkBackendAdmissionKey key,
        Supplier<Boolean> attempt,
        Runnable cleanup) {
        ZLinkBackendObject admissionSource = backend.admissionSource();
        return source(admissionSource).submit(
            key,
            attempt,
            cleanup,
            normalizedTimeoutMillis(admissionSource.admissionTimeout()));
    }

    static int normalizedTimeoutMillis(Duration timeout) {
        if (timeout == null || timeout.isZero() || timeout.isNegative()) {
            throw new IllegalArgumentException("send timeout must be positive");
        }
        long seconds = timeout.getSeconds();
        int nanos = timeout.getNano();
        if (seconds > Integer.MAX_VALUE / 1000L) {
            throw new IllegalArgumentException("send timeout exceeds Integer.MAX_VALUE ms");
        }
        long millis = seconds * 1000L + (nanos + 999_999L) / 1_000_000L;
        if (millis < 1L || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(
                "send timeout must normalize to 1..Integer.MAX_VALUE ms");
        }
        return (int) millis;
    }

    private static Source source(ZLinkBackendObject backend) {
        synchronized (SOURCES) {
            Source current = SOURCES.get(backend);
            if (current != null) {
                return current;
            }
            Source created = new Source(backend.admissionPendingCapacity());
            SOURCES.put(backend, created);
            backend.setAdmissionReadyHandler(created::ready);
            backend.setAdmissionShutdownHandler(created::shutdown);
            return created;
        }
    }

    private static final class Source {
        private final int pendingCapacity;
        private final Object lock = new Object();
        private final Map<ZLinkBackendAdmissionKey, ArrayDeque<Pending>> queues =
            new HashMap<>();
        private final Map<ZLinkBackendAdmissionKey, Integer> readyCredits =
            new HashMap<>();
        private final Set<Pending> pending = new HashSet<>();
        private int pendingCount;
        private boolean shutdown;

        Source(int pendingCapacity) {
            if (pendingCapacity <= 0) {
                throw new IllegalArgumentException(
                    "pending admission capacity must be positive");
            }
            this.pendingCapacity = pendingCapacity;
        }

        CompletionStage<ZLinkSubmitResult> submit(
            ZLinkBackendAdmissionKey key,
            Supplier<Boolean> attempt,
            Runnable cleanup,
            int timeoutMillis) {
            Pending item = new Pending(this, key, attempt, cleanup);
            if (item.attemptOnce() == AttemptResult.RETRY) {
                drive(item, timeoutMillis);
            }
            return item.future;
        }

        void ready(ZLinkBackendAdmissionKey key) {
            Pending item;
            synchronized (lock) {
                if (shutdown) {
                    return;
                }
                ArrayDeque<Pending> queue = queues.get(key);
                item = queue == null ? null : pollLive(queue);
                if (queue != null && queue.isEmpty()) {
                    queues.remove(key);
                }
                if (item == null) {
                    // Preserve one edge that races between first EAGAIN and enqueue.
                    readyCredits.put(key, 1);
                    return;
                }
            }
            if (item.attemptOnce() == AttemptResult.RETRY) {
                drive(item, 0);
            }
        }

        void shutdown() {
            ArrayList<Pending> terminal;
            synchronized (lock) {
                if (shutdown) {
                    return;
                }
                shutdown = true;
                terminal = new ArrayList<>(pending);
                for (Pending item : terminal) {
                    markDoneLocked(item);
                }
                queues.clear();
                readyCredits.clear();
            }
            for (Pending item : terminal) {
                item.completeMarked(
                    ZLinkSubmitResults.result(ZLinkSubmitStatus.SHUTDOWN));
            }
        }

        private void drive(Pending item, int timeoutMillis) {
            while (true) {
                AwaitResult wait = reserveOrAwait(item, timeoutMillis);
                if (wait == AwaitResult.QUEUED) {
                    return;
                }
                if (wait == AwaitResult.RETRY) {
                    if (item.attemptOnce() == AttemptResult.RETRY) {
                        continue;
                    }
                    return;
                }
                if (wait == AwaitResult.BACKPRESSURED) {
                    item.completeMarked(ZLinkSubmitResults.result(
                        ZLinkSubmitStatus.BACKPRESSURED));
                } else if (wait == AwaitResult.SHUTDOWN) {
                    item.completeMarked(ZLinkSubmitResults.result(
                        ZLinkSubmitStatus.SHUTDOWN));
                }
                return;
            }
        }

        private AwaitResult reserveOrAwait(Pending item, int timeoutMillis) {
            synchronized (lock) {
                if (item.done) {
                    return AwaitResult.TERMINAL;
                }
                if (shutdown) {
                    markDoneLocked(item);
                    return AwaitResult.SHUTDOWN;
                }
                if (!item.reserved) {
                    if (pendingCount >= pendingCapacity) {
                        markDoneLocked(item);
                        return AwaitResult.BACKPRESSURED;
                    }
                    item.reserved = true;
                    pending.add(item);
                    pendingCount++;
                    item.deadline = DEADLINES.schedule(
                        item::timeout,
                        timeoutMillis,
                        TimeUnit.MILLISECONDS);
                }
                if (readyCredits.remove(item.key) != null) {
                    return AwaitResult.RETRY;
                }
                enqueueLocked(item);
                return AwaitResult.QUEUED;
            }
        }

        private Pending pollLive(ArrayDeque<Pending> queue) {
            Pending item;
            while ((item = queue.pollFirst()) != null) {
                item.queued = false;
                if (!item.done) {
                    return item;
                }
            }
            return null;
        }

        private void enqueueLocked(Pending item) {
            queues.computeIfAbsent(item.key, ignored -> new ArrayDeque<>())
                .addLast(item);
            item.queued = true;
        }

        boolean markDone(Pending item) {
            synchronized (lock) {
                return markDoneLocked(item);
            }
        }

        private boolean markDoneLocked(Pending item) {
            if (item.done) {
                return false;
            }
            item.done = true;
            if (item.queued) {
                ArrayDeque<Pending> queue = queues.get(item.key);
                if (queue != null) {
                    queue.remove(item);
                    if (queue.isEmpty()) {
                        queues.remove(item.key);
                    }
                }
                item.queued = false;
            }
            if (item.reserved) {
                pending.remove(item);
                item.reserved = false;
                pendingCount--;
            }
            return true;
        }
    }

    private enum AttemptResult {
        TERMINAL,
        RETRY
    }

    private enum AwaitResult {
        QUEUED,
        RETRY,
        BACKPRESSURED,
        SHUTDOWN,
        TERMINAL
    }

    private static final class Pending {
        private final Source source;
        private final ZLinkBackendAdmissionKey key;
        private final Supplier<Boolean> attempt;
        private final Runnable cleanup;
        private final AdmissionFuture future;
        private boolean queued;
        private boolean reserved;
        private boolean done;
        private boolean cleaned;
        private ScheduledFuture<?> deadline;

        Pending(
            Source source,
            ZLinkBackendAdmissionKey key,
            Supplier<Boolean> attempt,
            Runnable cleanup) {
            this.source = source;
            this.key = key;
            this.attempt = attempt;
            this.cleanup = cleanup;
            this.future = new AdmissionFuture(this);
        }

        AttemptResult attemptOnce() {
            ZLinkSubmitResult terminal = null;
            Throwable failure = null;
            synchronized (source.lock) {
                if (done) {
                    return AttemptResult.TERMINAL;
                }
                if (source.shutdown) {
                    source.markDoneLocked(this);
                    terminal = ZLinkSubmitResults.result(ZLinkSubmitStatus.SHUTDOWN);
                } else {
                    try {
                        if (attempt.get()) {
                            source.markDoneLocked(this);
                            terminal = ZLinkSubmitResults.result(
                                ZLinkSubmitStatus.SUBMITTED);
                        } else {
                            return AttemptResult.RETRY;
                        }
                    } catch (RuntimeException error) {
                        ZLinkSubmitResult mapped = ZLinkSubmitResults.fromFailure(error);
                        if (mapped != null
                            && mapped.status() == ZLinkSubmitStatus.BACKPRESSURED) {
                            return AttemptResult.RETRY;
                        }
                        source.markDoneLocked(this);
                        if (mapped != null) {
                            terminal = mapped;
                        } else {
                            failure = error;
                        }
                    }
                }
            }
            if (failure != null) {
                completeExceptionallyMarked(failure);
            } else {
                completeMarked(terminal);
            }
            return AttemptResult.TERMINAL;
        }

        void timeout() {
            if (!source.markDone(this)) {
                return;
            }
            completeMarked(ZLinkSubmitResults.result(ZLinkSubmitStatus.TIMED_OUT));
        }

        boolean cancel() {
            if (!source.markDone(this)) {
                return false;
            }
            cancelDeadline();
            try {
                return future.cancelTerminal();
            } finally {
                cleanupOnce();
            }
        }

        void completeMarked(ZLinkSubmitResult result) {
            cancelDeadline();
            try {
                future.completeTerminal(result);
            } finally {
                cleanupOnce();
            }
        }

        void completeExceptionallyMarked(Throwable error) {
            cancelDeadline();
            try {
                future.completeExceptionallyTerminal(error);
            } finally {
                cleanupOnce();
            }
        }

        private void cancelDeadline() {
            ScheduledFuture<?> task = deadline;
            if (task != null) {
                task.cancel(false);
            }
        }

        private void cleanupOnce() {
            synchronized (source.lock) {
                if (cleaned) {
                    return;
                }
                cleaned = true;
            }
            try {
                cleanup.run();
            } catch (RuntimeException error) {
                LOGGER.log(Level.WARNING, "one-way submission payload cleanup failed", error);
            }
        }
    }

    private static final class AdmissionFuture
        extends CompletableFuture<ZLinkSubmitResult> {
        private final Pending pending;

        AdmissionFuture(Pending pending) {
            this.pending = pending;
        }

        @Override
        public boolean cancel(boolean mayInterruptIfRunning) {
            return pending.cancel();
        }

        boolean cancelTerminal() {
            return super.cancel(false);
        }

        boolean completeTerminal(ZLinkSubmitResult result) {
            return super.complete(result);
        }

        boolean completeExceptionallyTerminal(Throwable error) {
            return super.completeExceptionally(error);
        }
    }
}
