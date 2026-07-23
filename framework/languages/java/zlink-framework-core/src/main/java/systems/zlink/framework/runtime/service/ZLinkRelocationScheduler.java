package systems.zlink.framework.runtime.service;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;

/**
 * Readiness-driven relocation scheduler. A unit starts as soon as its current
 * application turn completes, subject to both the unit and payload budgets.
 */
public final class ZLinkRelocationScheduler {
    public static final int DEFAULT_MAX_CONCURRENT_UNITS = 64;
    public static final long DEFAULT_MAX_IN_FLIGHT_BYTES =
        256L * 1024 * 1024;

    private final int maxConcurrentUnits;
    private final long maxInFlightBytes;

    public ZLinkRelocationScheduler() {
        this(
            DEFAULT_MAX_CONCURRENT_UNITS,
            DEFAULT_MAX_IN_FLIGHT_BYTES);
    }

    public ZLinkRelocationScheduler(
        int maxConcurrentUnits,
        long maxInFlightBytes) {
        if (maxConcurrentUnits <= 0 || maxInFlightBytes <= 0) {
            throw new IllegalArgumentException(
                "relocation budgets must be positive");
        }
        this.maxConcurrentUnits = maxConcurrentUnits;
        this.maxInFlightBytes = maxInFlightBytes;
    }

    public Run schedule(Collection<Unit> input) {
        Objects.requireNonNull(input, "input");
        State state = new State(List.copyOf(input));
        state.start();
        return new Run(
            state.completion,
            state::snapshot);
    }

    public record Unit(
        String id,
        long payloadBytes,
        CompletionStage<Void> ready,
        Supplier<CompletionStage<Void>> relocate) {
        public Unit {
            if (id == null || id.isBlank()) {
                throw new IllegalArgumentException("relocation unit id is required");
            }
            if (payloadBytes < 0) {
                throw new IllegalArgumentException(
                    "relocation payload bytes must not be negative");
            }
            Objects.requireNonNull(ready, "ready");
            Objects.requireNonNull(relocate, "relocate");
        }
    }

    public record Snapshot(
        int totalUnits,
        int waitingUnits,
        int readyUnits,
        int activeUnits,
        int completedUnits,
        long inFlightBytes,
        boolean terminal) {
    }

    public static final class Run {
        private final CompletionStage<Void> completion;
        private final Supplier<Snapshot> snapshot;

        private Run(
            CompletionStage<Void> completion,
            Supplier<Snapshot> snapshot) {
            this.completion = completion;
            this.snapshot = snapshot;
        }

        public CompletionStage<Void> completion() {
            return completion;
        }

        public Snapshot snapshot() {
            return snapshot.get();
        }
    }

    private final class State {
        private final List<Unit> units;
        private final ArrayDeque<Unit> ready = new ArrayDeque<>();
        private final CompletableFuture<Void> completion =
            new CompletableFuture<>();
        private int waiting;
        private int active;
        private int completed;
        private long inFlightBytes;
        private boolean failed;

        State(List<Unit> units) {
            this.units = units;
            waiting = units.size();
        }

        void start() {
            if (units.isEmpty()) {
                completion.complete(null);
                return;
            }
            for (Unit unit : units) {
                unit.ready().whenComplete((ignored, failure) ->
                    ready(unit, failure));
            }
        }

        private synchronized void ready(Unit unit, Throwable failure) {
            waiting--;
            if (failure != null) {
                fail(failure);
                return;
            }
            ready.addLast(unit);
            drain();
        }

        private synchronized void drain() {
            if (failed) {
                return;
            }
            while (active < maxConcurrentUnits && !ready.isEmpty()) {
                Unit selected = selectFitting();
                if (selected == null) {
                    return;
                }
                active++;
                inFlightBytes += selected.payloadBytes();
                CompletionStage<Void> relocation;
                try {
                    relocation = Objects.requireNonNull(
                        selected.relocate().get(),
                        "relocation action returned null");
                } catch (Throwable failure) {
                    finish(selected, failure);
                    continue;
                }
                relocation.whenComplete((ignored, failure) ->
                    finish(selected, failure));
            }
            completeIfDone();
        }

        private Unit selectFitting() {
            List<Unit> deferred = new ArrayList<>();
            Unit selected = null;
            while (!ready.isEmpty()) {
                Unit candidate = ready.removeFirst();
                boolean oversized = candidate.payloadBytes()
                    > maxInFlightBytes;
                boolean fits = oversized
                    ? active == 0
                    : inFlightBytes + candidate.payloadBytes()
                        <= maxInFlightBytes;
                if (fits) {
                    selected = candidate;
                    break;
                }
                deferred.add(candidate);
            }
            deferred.forEach(ready::addLast);
            return selected;
        }

        private synchronized void finish(Unit unit, Throwable failure) {
            active--;
            inFlightBytes -= unit.payloadBytes();
            if (failure != null) {
                fail(failure);
                return;
            }
            completed++;
            drain();
        }

        private void fail(Throwable failure) {
            failed = true;
            completion.completeExceptionally(failure);
        }

        private void completeIfDone() {
            if (!failed
                && waiting == 0
                && ready.isEmpty()
                && active == 0
                && completed == units.size()) {
                completion.complete(null);
            }
        }

        synchronized Snapshot snapshot() {
            return new Snapshot(
                units.size(),
                waiting,
                ready.size(),
                active,
                completed,
                inFlightBytes,
                completion.isDone());
        }
    }
}
