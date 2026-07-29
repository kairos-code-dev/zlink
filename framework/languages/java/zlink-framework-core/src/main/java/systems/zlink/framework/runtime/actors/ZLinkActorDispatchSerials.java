package systems.zlink.framework.runtime.actors;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import java.util.function.Function;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;

final class ZLinkActorDispatchSerials {
    private final Object runtimeScope;
    private final Function<String, Object> incarnationResolver;
    private final Map<String, ZLinkAsyncSerialQueue> queues = new HashMap<>();
    private final Set<String> activeActorIds = new HashSet<>();
    private final Map<String, CompletionStage<Void>> teardowns = new HashMap<>();

    ZLinkActorDispatchSerials() {
        this(ZLinkDeferredActorJoinScope.legacyRuntimeScope(), actorId -> actorId);
    }

    ZLinkActorDispatchSerials(
        Object runtimeScope,
        Function<String, Object> incarnationResolver) {
        this.runtimeScope = java.util.Objects.requireNonNull(
            runtimeScope, "runtimeScope");
        this.incarnationResolver = java.util.Objects.requireNonNull(
            incarnationResolver, "incarnationResolver");
    }

    boolean isCurrent(String actorId) {
        return actorId.equals(systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.currentActorDispatch());
    }

    synchronized boolean isActive(String actorId) {
        return activeActorIds.contains(actorId);
    }

    synchronized QueuedTurn prepare(String actorId) {
        if (teardowns.containsKey(actorId)) {
            throw new IllegalStateException(
                "actor dispatch admission is closed: " + actorId);
        }
        return new QueuedTurn(
            actorId,
            queues.computeIfAbsent(actorId, ignored -> new ZLinkAsyncSerialQueue(false)));
    }

    synchronized ZLinkAsyncSerialQueue relocationLane(String actorId) {
        return queues.computeIfAbsent(
            actorId, ignored -> new ZLinkAsyncSerialQueue(false));
    }

    synchronized void remove(String actorId) {
        queues.remove(actorId);
        activeActorIds.remove(actorId);
        teardowns.remove(actorId);
    }

    CompletionStage<Void> enqueue(
        QueuedTurn turn,
        Supplier<CompletionStage<Void>> operation) {
        return enqueue(turn, null, operation);
    }

    CompletionStage<Void> enqueue(
        QueuedTurn turn,
        byte[] acceptedJournalRecord,
        Supplier<CompletionStage<Void>> operation) {
        return enqueue(
            turn, acceptedJournalRecord, operation, () -> { });
    }

    synchronized CompletionStage<Void> enqueue(
        QueuedTurn turn,
        byte[] acceptedJournalRecord,
        Supplier<CompletionStage<Void>> operation,
        Runnable relocationRelease) {
        if (teardowns.containsKey(turn.actorId)) {
            return CompletableFuture.failedFuture(
                new IllegalStateException(
                    "actor dispatch admission is closed: " + turn.actorId));
        }
        Supplier<CompletionStage<Void>> turnOperation = () -> {
            synchronized (this) {
                activeActorIds.add(turn.actorId);
            }
            return runTurn(turn.actorId, operation)
                .whenComplete((ignored, error) -> {
                    synchronized (this) {
                        activeActorIds.remove(turn.actorId);
                    }
                });
        };
        return acceptedJournalRecord == null
            ? turn.queue.enqueue(turnOperation)
            : turn.queue.enqueueRelocatable(
                acceptedJournalRecord,
                turnOperation,
                relocationRelease);
    }

    CompletionStage<Void> beginTeardown(
        String actorId,
        Supplier<CompletionStage<Void>> cleanup) {
        CompletionStage<Void> teardown;
        synchronized (this) {
            teardown = teardowns.get(actorId);
            if (teardown == null) {
                ZLinkAsyncSerialQueue queue = queues.computeIfAbsent(
                    actorId,
                    ignored -> new ZLinkAsyncSerialQueue(false));
                CompletableFuture<Void> terminal = new CompletableFuture<>();
                teardowns.put(actorId, terminal);
                queue.enqueueLifecycleBarrier(cleanup)
                    .whenComplete((ignored, error) -> {
                        synchronized (this) {
                            teardowns.remove(actorId, terminal);
                            if (error == null) {
                                queues.remove(actorId, queue);
                                activeActorIds.remove(actorId);
                            }
                        }
                        if (error == null) {
                            terminal.complete(null);
                        } else {
                            terminal.completeExceptionally(error);
                        }
                    });
                teardown = terminal;
            }
        }
        // Waiting for a barrier queued behind the current turn would make the
        // current Actor handler wait for itself. Cleanup still runs next, but
        // the initiating turn may complete normally.
        return isCurrent(actorId)
            ? CompletableFuture.completedFuture(null)
            : teardown;
    }

    CompletionStage<Void> enqueueBarrier(
        String actorId,
        Supplier<CompletionStage<Void>> operation) {
        ZLinkAsyncSerialQueue queue;
        synchronized (this) {
            queue = queues.computeIfAbsent(
                actorId,
                ignored -> new ZLinkAsyncSerialQueue(false));
        }
        return queue.enqueueBarrierNext(() -> runTurn(actorId, operation));
    }

    synchronized Optional<ZLinkAsyncSerialQueue.RelocationSeal> trySeal(
        String actorId) {
        return relocationLane(actorId).trySealRelocation();
    }

    synchronized boolean abort(
        String actorId,
        ZLinkAsyncSerialQueue.RelocationSeal seal) {
        ZLinkAsyncSerialQueue queue = queues.get(actorId);
        return queue != null && queue.abortRelocation(seal);
    }

    synchronized Optional<List<ZLinkAsyncSerialQueue.QueuedRecord>> commit(
        String actorId,
        ZLinkAsyncSerialQueue.RelocationSeal seal) {
        ZLinkAsyncSerialQueue queue = queues.get(actorId);
        return queue == null
            ? Optional.empty()
            : queue.commitRelocation(seal);
    }

    synchronized Optional<List<ZLinkAsyncSerialQueue.QueuedRecord>>
        freezeIngress(
            String actorId,
            ZLinkAsyncSerialQueue.RelocationSeal seal) {
        ZLinkAsyncSerialQueue queue = queues.get(actorId);
        return queue == null
            ? Optional.empty()
            : queue.freezeRelocationIngress(seal);
    }

    synchronized CompletionStage<Void> awaitQuiescence() {
        CompletableFuture<?>[] barriers = queues.values().stream()
            .map(ZLinkAsyncSerialQueue::awaitQuiescence)
            .map(CompletionStage::toCompletableFuture)
            .toArray(CompletableFuture[]::new);
        return CompletableFuture.allOf(barriers);
    }

    <T> CompletionStage<T> runTurn(
        String actorId,
        Supplier<CompletionStage<T>> operation) {
        try (systems.zlink.framework.runtime.internal.handlers
                 .ZLinkSuspendInvocationContext.Scope ignored =
                 systems.zlink.framework.runtime.internal.handlers
                     .ZLinkSuspendInvocationContext.enterActorDispatch(actorId);
             ZLinkDeferredActorJoinScope.Scope joins =
                 ZLinkDeferredActorJoinScope.enter(
                     runtimeScope,
                     java.util.Objects.requireNonNull(
                         incarnationResolver.apply(actorId),
                         "actor incarnation"),
                     actorId)) {
            CompletionStage<T> handler = operation.get();
            CompletableFuture<T> completed = new CompletableFuture<>();
            joins.finish(handler, null).whenComplete((nothing, error) -> {
                if (error != null) {
                    completed.completeExceptionally(error);
                    return;
                }
                handler.whenComplete((value, handlerError) -> {
                    if (handlerError != null) {
                        completed.completeExceptionally(handlerError);
                    } else {
                        completed.complete(value);
                    }
                });
            });
            return completed;
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    record QueuedTurn(String actorId, ZLinkAsyncSerialQueue queue) {
    }
}
