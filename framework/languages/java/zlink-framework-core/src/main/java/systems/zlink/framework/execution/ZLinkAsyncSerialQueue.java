package systems.zlink.framework.execution;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Executor;
import java.util.function.Supplier;
import systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext;

public final class ZLinkAsyncSerialQueue {
    private static final ExecutorService HANDLER_EXECUTOR =
        Executors.newVirtualThreadPerTaskExecutor();
    private static final ThreadLocal<ZLinkAsyncSerialQueue> CURRENT = new ThreadLocal<>();
    private static final ThreadLocal<CompletableFuture<Void>> CURRENT_GATE = new ThreadLocal<>();
    private static final ThreadLocal<Boolean> CURRENT_RELEASE_DEFERRED = new ThreadLocal<>();

    private final boolean releaseOnIncompleteStage;
    private final int pendingCapacity;
    private final ArrayDeque<Entry> pending = new ArrayDeque<>();
    private int outstanding;
    private long nextSequence = 1L;
    private long nextRelocationSerial = 1L;
    private Entry active;
    private RelocationState relocation;
    private boolean relocated;
    private Runnable capacityAvailable = () -> { };

    public ZLinkAsyncSerialQueue() {
        this(false, Integer.MAX_VALUE);
    }

    public ZLinkAsyncSerialQueue(boolean releaseOnIncompleteStage) {
        this(releaseOnIncompleteStage, Integer.MAX_VALUE);
    }

    public ZLinkAsyncSerialQueue(boolean releaseOnIncompleteStage, int pendingCapacity) {
        if (pendingCapacity <= 0) {
            throw new IllegalArgumentException("pendingCapacity must be positive");
        }
        this.releaseOnIncompleteStage = releaseOnIncompleteStage;
        this.pendingCapacity = pendingCapacity;
    }

    public synchronized CompletionStage<Void> enqueue(Supplier<CompletionStage<Void>> operation) {
        return enqueueAccepted(null, operation);
    }

    public synchronized CompletionStage<Void> enqueueRelocatable(
        byte[] record,
        Supplier<CompletionStage<Void>> operation) {
        return enqueueRelocatable(record, operation, () -> { });
    }

    public synchronized CompletionStage<Void> enqueueRelocatable(
        byte[] record,
        Supplier<CompletionStage<Void>> operation,
        Runnable relocationRelease) {
        java.util.Objects.requireNonNull(record, "record");
        java.util.Objects.requireNonNull(relocationRelease, "relocationRelease");
        if (relocated) {
            return CompletableFuture.failedFuture(
                new IllegalStateException("queue owner has relocated"));
        }
        return enqueueAccepted(record.clone(), operation, relocationRelease);
    }

    public synchronized boolean tryEnqueue(Supplier<CompletionStage<Void>> operation) {
        if (outstanding > pendingCapacity) {
            return false;
        }
        enqueueAccepted(null, operation);
        return true;
    }

    public synchronized boolean tryEnqueueRelocatable(
        byte[] record,
        Supplier<CompletionStage<Void>> operation) {
        java.util.Objects.requireNonNull(record, "record");
        if (relocated || outstanding > pendingCapacity) {
            return false;
        }
        enqueueAccepted(record.clone(), operation);
        return true;
    }

    public synchronized void onCapacityAvailable(Runnable callback) {
        capacityAvailable = java.util.Objects.requireNonNull(callback, "callback");
    }

    private CompletionStage<Void> enqueueAccepted(
        byte[] record,
        Supplier<CompletionStage<Void>> operation) {
        return enqueueAccepted(record, operation, () -> { });
    }

    private CompletionStage<Void> enqueueAccepted(
        byte[] record,
        Supplier<CompletionStage<Void>> operation,
        Runnable relocationRelease) {
        java.util.Objects.requireNonNull(operation, "operation");
        if (nextSequence == Long.MAX_VALUE) {
            throw new IllegalStateException("queue sequence exhausted");
        }
        outstanding++;
        Entry entry = new Entry(
            nextSequence++,
            record,
            operation,
            relocationRelease,
            new CompletableFuture<>(),
            ZLinkFlowContext.current());
        if (record != null && relocation != null) {
            relocation.held.addLast(entry);
        } else {
            pending.addLast(entry);
            startNext();
        }
        return entry.result;
    }

    private void startNext() {
        if (active != null || pending.isEmpty()) {
            return;
        }
        Entry entry = pending.removeFirst();
        active = entry;
        CompletionStage<Void> gate = invoke(
            entry.operation,
            entry.result,
            entry.flow);
        gate.whenComplete((ignored, error) -> finish(entry));
    }

    private void finish(Entry entry) {
        Runnable notify = null;
        synchronized (this) {
            if (active != entry) {
                return;
            }
            active = null;
            boolean wasFull = outstanding > pendingCapacity;
            outstanding--;
            if (wasFull && outstanding <= pendingCapacity) {
                notify = capacityAvailable;
            }
            startNext();
        }
        if (notify != null) {
            HANDLER_EXECUTOR.execute(notify);
        }
    }

    public synchronized Optional<RelocationSeal> trySealRelocation() {
        if (relocated || relocation != null) {
            return Optional.empty();
        }
        if (active != null && CURRENT.get() != this) {
            return Optional.empty();
        }
        if (nextRelocationSerial == Long.MAX_VALUE) {
            throw new IllegalStateException("relocation serial exhausted");
        }
        ArrayDeque<Entry> captured = new ArrayDeque<>();
        ArrayDeque<Entry> infrastructure = new ArrayDeque<>();
        while (!pending.isEmpty()) {
            Entry entry = pending.removeFirst();
            if (entry.record == null) {
                infrastructure.addLast(entry);
            } else {
                captured.addLast(entry);
            }
        }
        pending.addAll(infrastructure);
        long serial = nextRelocationSerial++;
        relocation = new RelocationState(serial, captured);
        return Optional.of(new RelocationSeal(
            serial,
            captured.stream().map(Entry::queuedRecord).toList()));
    }

    public synchronized boolean abortRelocation(RelocationSeal seal) {
        if (!matches(seal)) {
            return false;
        }
        ArrayDeque<Entry> restored = new ArrayDeque<>();
        while (!pending.isEmpty()) {
            restored.addLast(pending.removeFirst());
        }
        restored.addAll(relocation.captured);
        restored.addAll(relocation.held);
        pending.addAll(restored);
        relocation = null;
        startNext();
        return true;
    }

    public synchronized Optional<List<QueuedRecord>> commitRelocation(
        RelocationSeal seal) {
        if (!matches(seal)) {
            return Optional.empty();
        }
        List<QueuedRecord> held = relocation.held.stream()
            .map(Entry::queuedRecord)
            .toList();
        List<Entry> released = new ArrayList<>(
            relocation.captured.size() + relocation.held.size());
        released.addAll(relocation.captured);
        released.addAll(relocation.held);
        relocation = null;
        relocated = true;
        for (Entry entry : released) {
            try {
                entry.relocationRelease.run();
                entry.result.complete(null);
            } catch (RuntimeException failure) {
                entry.result.completeExceptionally(failure);
            } finally {
                releaseRelocatedCapacity();
            }
        }
        return Optional.of(held);
    }

    private boolean matches(RelocationSeal seal) {
        return seal != null
            && relocation != null
            && relocation.serial == seal.serial;
    }

    private void releaseRelocatedCapacity() {
        boolean wasFull = outstanding > pendingCapacity;
        outstanding--;
        if (wasFull && outstanding <= pendingCapacity) {
            HANDLER_EXECUTOR.execute(capacityAvailable);
        }
    }

    private CompletionStage<Void> invoke(
        Supplier<CompletionStage<Void>> operation,
        CompletableFuture<Void> result,
        ZLinkFlowContext.State flow) {
        CompletableFuture<Void> gate = new CompletableFuture<>();
        HANDLER_EXECUTOR.execute(() -> {
            ZLinkAsyncSerialQueue previous = CURRENT.get();
            CompletableFuture<Void> previousGate = CURRENT_GATE.get();
            Boolean previousDeferred = CURRENT_RELEASE_DEFERRED.get();
            CURRENT.set(this);
            CURRENT_GATE.set(gate);
            CURRENT_RELEASE_DEFERRED.set(false);
            try (ZLinkFlowContext.Scope ignored = flow == null
                ? () -> { }
                : ZLinkFlowContext.enter(flow)) {
                CompletionStage<Void> execution = java.util.Objects.requireNonNull(
                    operation.get(), "operation result");
                execution.whenComplete((value, error) -> {
                    if (error != null) {
                        result.completeExceptionally(error);
                    } else {
                        result.complete(null);
                    }
                    if (!releaseOnIncompleteStage) {
                        gate.complete(null);
                    }
                });
                if (releaseOnIncompleteStage
                    && !Boolean.TRUE.equals(CURRENT_RELEASE_DEFERRED.get())) {
                    gate.complete(null);
                }
            } catch (RuntimeException error) {
                result.completeExceptionally(error);
                gate.complete(null);
            } finally {
                if (previous == null) {
                    CURRENT.remove();
                } else {
                    CURRENT.set(previous);
                }
                if (previousGate == null) {
                    CURRENT_GATE.remove();
                } else {
                    CURRENT_GATE.set(previousGate);
                }
                if (previousDeferred == null) {
                    CURRENT_RELEASE_DEFERRED.remove();
                } else {
                    CURRENT_RELEASE_DEFERRED.set(previousDeferred);
                }
            }
        });
        return gate;
    }

    public static <T> CompletionStage<T> manageCurrent(CompletionStage<T> stage) {
        java.util.Objects.requireNonNull(stage, "stage");
        ZLinkAsyncSerialQueue queue = CURRENT.get();
        if (queue == null) {
            return stage;
        }
        ZLinkFlowContext.State flow = ZLinkFlowContext.current();
        CompletableFuture<T> managed = new CompletableFuture<>();
        managed.whenComplete((ignored, error) -> {
            if (managed.isCancelled()) {
                stage.toCompletableFuture().cancel(false);
            }
        });
        if (!queue.releaseOnIncompleteStage) {
            CompletableFuture<Void> gate = CURRENT_GATE.get();
            stage.whenComplete((value, error) -> HANDLER_EXECUTOR.execute(() -> {
                ZLinkAsyncSerialQueue previous = CURRENT.get();
                CompletableFuture<Void> previousGate = CURRENT_GATE.get();
                CURRENT.set(queue);
                if (gate == null) {
                    CURRENT_GATE.remove();
                } else {
                    CURRENT_GATE.set(gate);
                }
                try (ZLinkFlowContext.Scope ignored = flow == null
                    ? () -> { }
                    : ZLinkFlowContext.enter(flow)) {
                    if (error != null) {
                        managed.completeExceptionally(error);
                    } else {
                        managed.complete(value);
                    }
                } finally {
                    if (previous == null) {
                        CURRENT.remove();
                    } else {
                        CURRENT.set(previous);
                    }
                    if (previousGate == null) {
                        CURRENT_GATE.remove();
                    } else {
                        CURRENT_GATE.set(previousGate);
                    }
                }
            }));
            return managed;
        }
        stage.whenComplete((value, error) -> queue.enqueue(() -> {
            try (ZLinkFlowContext.Scope ignored = flow == null
                ? () -> { }
                : ZLinkFlowContext.enter(flow)) {
                if (error != null) {
                    managed.completeExceptionally(error);
                } else {
                    managed.complete(value);
                }
            }
            return CompletableFuture.completedFuture(null);
        }));
        return managed;
    }

    public static <T> CompletionStage<T> yieldCurrent(CompletionStage<T> stage) {
        java.util.Objects.requireNonNull(stage, "stage");
        ZLinkAsyncSerialQueue queue = CURRENT.get();
        CompletableFuture<Void> gate = CURRENT_GATE.get();
        if (queue == null || gate == null || stage.toCompletableFuture().isDone()) {
            return stage;
        }
        ZLinkFlowContext.State flow = ZLinkFlowContext.current();
        CompletableFuture<T> managed = new CompletableFuture<>();
        managed.whenComplete((ignored, error) -> {
            if (managed.isCancelled()) {
                stage.toCompletableFuture().cancel(false);
            }
        });
        gate.complete(null);
        stage.whenComplete((value, error) -> queue.enqueue(() -> {
            try (ZLinkFlowContext.Scope ignored = flow == null
                ? () -> { }
                : ZLinkFlowContext.enter(flow)) {
                if (error != null) {
                    managed.completeExceptionally(error);
                } else {
                    managed.complete(value);
                }
            }
            return CompletableFuture.completedFuture(null);
        }));
        return managed;
    }

    public static Executor propagateCurrent(Executor executor) {
        java.util.Objects.requireNonNull(executor, "executor");
        return command -> {
            ZLinkAsyncSerialQueue queue = CURRENT.get();
            CompletableFuture<Void> gate = CURRENT_GATE.get();
            Boolean deferred = CURRENT_RELEASE_DEFERRED.get();
            executor.execute(() -> runWithContext(queue, gate, deferred, command));
        };
    }

    private static void runWithContext(
        ZLinkAsyncSerialQueue queue,
        CompletableFuture<Void> gate,
        Boolean deferred,
        Runnable command) {
        ZLinkAsyncSerialQueue previous = CURRENT.get();
        CompletableFuture<Void> previousGate = CURRENT_GATE.get();
        Boolean previousDeferred = CURRENT_RELEASE_DEFERRED.get();
        setOrRemove(CURRENT, queue);
        setOrRemove(CURRENT_GATE, gate);
        setOrRemove(CURRENT_RELEASE_DEFERRED, deferred);
        try {
            command.run();
        } finally {
            setOrRemove(CURRENT, previous);
            setOrRemove(CURRENT_GATE, previousGate);
            setOrRemove(CURRENT_RELEASE_DEFERRED, previousDeferred);
        }
    }

    private static <T> void setOrRemove(ThreadLocal<T> local, T value) {
        if (value == null) {
            local.remove();
        } else {
            local.set(value);
        }
    }

    public static <T> CompletionStage<T> deferCurrentReleaseUntil(CompletionStage<T> entered) {
        java.util.Objects.requireNonNull(entered, "entered");
        CompletableFuture<Void> gate = CURRENT_GATE.get();
        if (gate == null) {
            return entered;
        }
        CURRENT_RELEASE_DEFERRED.set(true);
        entered.whenComplete((ignored, error) -> gate.complete(null));
        return entered;
    }

    public record QueuedRecord(long sequence, byte[] payload) {
        public QueuedRecord {
            if (sequence <= 0) {
                throw new IllegalArgumentException(
                    "queue record sequence must be positive");
            }
            payload = java.util.Objects.requireNonNull(
                payload,
                "payload").clone();
        }

        @Override
        public byte[] payload() {
            return payload.clone();
        }
    }

    public record RelocationSeal(
        long serial,
        List<QueuedRecord> captured) {
        public RelocationSeal {
            if (serial <= 0) {
                throw new IllegalArgumentException(
                    "relocation seal serial must be positive");
            }
            captured = List.copyOf(captured);
        }
    }

    private static final class Entry {
        private final long sequence;
        private final byte[] record;
        private final Supplier<CompletionStage<Void>> operation;
        private final Runnable relocationRelease;
        private final CompletableFuture<Void> result;
        private final ZLinkFlowContext.State flow;

        private Entry(
            long sequence,
            byte[] record,
            Supplier<CompletionStage<Void>> operation,
            Runnable relocationRelease,
            CompletableFuture<Void> result,
            ZLinkFlowContext.State flow) {
            this.sequence = sequence;
            this.record = record;
            this.operation = operation;
            this.relocationRelease = relocationRelease;
            this.result = result;
            this.flow = flow;
        }

        private QueuedRecord queuedRecord() {
            return new QueuedRecord(sequence, record);
        }
    }

    private static final class RelocationState {
        private final long serial;
        private final ArrayDeque<Entry> captured;
        private final ArrayDeque<Entry> held = new ArrayDeque<>();

        private RelocationState(
            long serial,
            ArrayDeque<Entry> captured) {
            this.serial = serial;
            this.captured = captured;
        }
    }
}
