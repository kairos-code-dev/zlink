package systems.zlink.framework.runtime.actors;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.function.Supplier;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;

/**
 * Handler-local registration scope for deferred Actor membership transitions.
 */
final class ZLinkDeferredActorJoinScope {
    static final int MAX_JOIN_COUNT = 64;
    static final int MAX_REQUEST_BYTES = 1024 * 1024;
    static final int MAX_TOTAL_REQUEST_BYTES = 8 * 1024 * 1024;

    private static final ConcurrentMap<String, State> CLAIMS = new ConcurrentHashMap<>();
    private static final ConcurrentMap<String, State> ACTIVE_ACTOR_SCOPES =
        new ConcurrentHashMap<>();
    private static final java.util.Set<State> ACTIVE_HANDLER_SCOPES =
        ConcurrentHashMap.newKeySet();

    private ZLinkDeferredActorJoinScope() {
    }

    static State current() {
        return (State) systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.currentDeferredActorJoin();
    }

    static Scope enter(String actorId) {
        return enter(
            actorId,
            candidate -> Objects.equals(actorId, candidate),
            true);
    }

    static Scope enterHandler(Predicate<String> actorAllowed) {
        return enter(null, Objects.requireNonNull(actorAllowed, "actorAllowed"), false);
    }

    private static Scope enter(
        String dispatchActorId,
        Predicate<String> actorAllowed,
        boolean actorScope) {
        ThreadLocal<Object> local = systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.deferredActorJoinThreadLocal();
        Object previous = local.get();
        State state = new State(dispatchActorId, actorAllowed);
        if (actorScope) {
            State active = ACTIVE_ACTOR_SCOPES.putIfAbsent(dispatchActorId, state);
            if (active != null) {
                state = active;
            }
        }
        if (!actorScope) {
            ACTIVE_HANDLER_SCOPES.add(state);
        }
        local.set(state);
        return new Scope(local, previous, state, actorScope);
    }

    static void register(
        String actorId,
        int requestBytes,
        long deadlineNanos,
        Supplier<CompletionStage<Void>> operation) {
        register(actorId, requestBytes, deadlineNanos, operation, null);
    }

    static void registerWithActorBarrier(
        String actorId,
        int requestBytes,
        long deadlineNanos,
        Supplier<CompletionStage<Void>> operation,
        Function<Supplier<CompletionStage<Void>>, CompletionStage<Void>>
            actorMailbox) {
        register(
            actorId,
            requestBytes,
            deadlineNanos,
            operation,
            Objects.requireNonNull(actorMailbox, "actorMailbox"));
    }

    private static void register(
        String actorId,
        int requestBytes,
        long deadlineNanos,
        Supplier<CompletionStage<Void>> operation,
        Function<Supplier<CompletionStage<Void>>, CompletionStage<Void>>
            actorMailbox) {
        State state = current();
        if (state == null) {
            state = ACTIVE_ACTOR_SCOPES.get(actorId);
        }
        if (state == null) {
            State matched = null;
            for (State candidate : ACTIVE_HANDLER_SCOPES) {
                if (!candidate.accepts(actorId)) {
                    continue;
                }
                if (matched != null && matched != candidate) {
                    throw failure(
                        ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                        "Actor join defer matches more than one open handler scope");
                }
                matched = candidate;
            }
            state = matched;
        }
        if (state == null) {
            throw failure(
                ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                "Actor join defer is only valid in an open Framework handler scope");
        }
        synchronized (state) {
            if (state.sealed) {
                throw failure(
                    ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                    "Actor join handler registration scope is closed");
            }
            if (!state.accepts(actorId)) {
                throw failure(
                    ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                    "Actor join defer must target a local Actor allowed by the current handler");
            }
            if (requestBytes < 0 || requestBytes > MAX_REQUEST_BYTES) {
                throw failure(
                    ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                    "Actor join request exceeds the 1 MiB encoded limit");
            }
            if (state.intents.size() >= MAX_JOIN_COUNT
                || state.requestBytes + requestBytes > MAX_TOTAL_REQUEST_BYTES) {
                throw failure(
                    ZLinkFrameworkErrorKind.INVALID_CONFIGURATION,
                    "Actor join registrations exceed the handler limit");
            }
            if (CLAIMS.putIfAbsent(actorId, state) != null) {
                throw failure(
                    ZLinkFrameworkErrorKind.ACTOR_MOVING,
                    "Actor already has a pending membership transition");
            }
            state.claimedActorIds.add(actorId);
            state.requestBytes += requestBytes;
            if (actorMailbox == null
                || Objects.equals(state.dispatchActorId, actorId)) {
                state.intents.add(new Intent(deadlineNanos, operation, () -> { }));
                return;
            }
            CompletableFuture<Boolean> activation = new CompletableFuture<>();
            CompletionStage<Void> barrier;
            try {
                barrier = actorMailbox.apply(() -> activation.thenCompose(active ->
                    active
                        ? operation.get()
                        : CompletableFuture.completedFuture(null)));
            } catch (RuntimeException error) {
                state.claimedActorIds.remove(actorId);
                state.requestBytes -= requestBytes;
                CLAIMS.remove(actorId, state);
                throw error;
            }
            state.intents.add(new Intent(
                deadlineNanos,
                () -> {
                    activation.complete(true);
                    return barrier;
                },
                () -> activation.complete(false)));
        }
    }

    private static ZLinkFrameworkException failure(
        ZLinkFrameworkErrorKind kind,
        String message) {
        return new ZLinkFrameworkException(kind, message);
    }

    /**
     * Runs one intent's own activation and absorbs its outcome so a single
     * failing join can neither fail the handler's terminal reply nor stall
     * the intents registered after it (see the barrier-latch note above).
     * The join's own completion -- accepted, rejected or failed -- still
     * reaches the caller through {@code onJoinCompleted}; this only stops it
     * from also becoming the dispatch stage's outcome.
     */
    private static CompletionStage<Void> neutralize(Intent intent) {
        try {
            CompletionStage<Void> operation = intent.operation().get();
            return operation == null
                ? CompletableFuture.completedFuture(null)
                : operation.handle((ignored, error) -> null);
        } catch (RuntimeException error) {
            return CompletableFuture.completedFuture(null);
        }
    }

    static final class Scope implements AutoCloseable {
        private final ThreadLocal<Object> local;
        private final Object previous;
        private final State state;
        private final boolean actorScope;
        private boolean closed;
        private boolean finished;

        private Scope(
            ThreadLocal<Object> local,
            Object previous,
            State state,
            boolean actorScope) {
            this.local = local;
            this.previous = previous;
            this.state = state;
            this.actorScope = actorScope;
        }

        CompletionStage<Void> finish(
            CompletionStage<?> handler,
            Supplier<CompletionStage<Void>> failureObserver) {
            finished = true;
            synchronized (state) {
                if (state.finishStarted) {
                    return handler.thenApply(ignored -> null);
                }
                state.finishStarted = true;
            }
            return handler.handle((ignored, error) -> error)
                .thenCompose(error -> {
                    synchronized (state) {
                        state.sealed = true;
                    }
                    removeActiveScope();
                    if (error != null) {
                        state.discardIntents();
                        state.releaseClaims();
                        CompletionStage<Void> observed = failureObserver == null
                            ? CompletableFuture.completedFuture(null)
                            : failureObserver.get();
                        return observed.thenCompose(nothing ->
                            CompletableFuture.failedFuture(unwrap(error)));
                    }
                    // Ledger route-mesh-11.0.0 §2.3: the handler reply is already
                    // terminal once the handler itself succeeds, so a deferred
                    // join's own failure must never replace it (parity with the
                    // Node/.NET decoupled shape: Node swallows the activation
                    // error after capturing the handler result, .NET posts each
                    // join independently and reports failure only through
                    // onJoinCompleted). Each intent's own outcome is neutralized
                    // before the next intent runs -- not only after the whole
                    // chain -- because a barrier-registered intent already has
                    // its activation latch reserved on the target actor's queue
                    // at register() time; skipping straight to the end on a
                    // failure would leave that latch (and the target's queue)
                    // stuck forever instead of merely dropping one join.
                    CompletionStage<Void> tail =
                        CompletableFuture.completedFuture(null);
                    for (Intent intent : List.copyOf(state.intents)) {
                        tail = tail.thenCompose(nothing -> neutralize(intent));
                    }
                    return tail.handle((nothing, activationError) -> {
                        state.releaseClaims();
                        return null;
                    });
                });
        }

        @Override
        public void close() {
            if (closed) {
                return;
            }
            closed = true;
            if (!finished) {
                state.sealed = true;
                removeActiveScope();
                state.discardIntents();
                state.releaseClaims();
            }
            if (previous == null) {
                local.remove();
            } else {
                local.set(previous);
            }
        }

        private void removeActiveScope() {
            if (actorScope) {
                ACTIVE_ACTOR_SCOPES.remove(state.dispatchActorId, state);
            } else {
                ACTIVE_HANDLER_SCOPES.remove(state);
            }
        }
    }

    static final class State {
        private final String dispatchActorId;
        private final Predicate<String> actorAllowed;
        private final List<Intent> intents = new ArrayList<>();
        private final List<String> claimedActorIds = new ArrayList<>();
        private int requestBytes;
        private volatile boolean sealed;
        private boolean finishStarted;

        private State(
            String dispatchActorId,
            Predicate<String> actorAllowed) {
            this.dispatchActorId = dispatchActorId;
            this.actorAllowed = Objects.requireNonNull(actorAllowed, "actorAllowed");
        }

        private boolean accepts(String actorId) {
            return actorId != null && actorAllowed.test(actorId);
        }

        private void releaseClaims() {
            for (String actorId : claimedActorIds) {
                CLAIMS.remove(actorId, this);
            }
            claimedActorIds.clear();
            intents.clear();
        }

        private void discardIntents() {
            for (Intent intent : List.copyOf(intents)) {
                intent.discard.run();
            }
        }
    }

    private record Intent(
        long deadlineNanos,
        Supplier<CompletionStage<Void>> operation,
        Runnable discard) {
    }

    private static Throwable unwrap(Throwable error) {
        return error instanceof CompletionException && error.getCause() != null
            ? error.getCause()
            : error;
    }
}
