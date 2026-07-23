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
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;

final class ZLinkActorDispatchSerials {
    private final Map<String, ZLinkAsyncSerialQueue> queues = new HashMap<>();
    private final Set<String> activeActorIds = new HashSet<>();

    boolean isCurrent(String actorId) {
        return actorId.equals(systems.zlink.framework.runtime.internal.handlers
            .ZLinkSuspendInvocationContext.currentActorDispatch());
    }

    synchronized boolean isActive(String actorId) {
        return activeActorIds.contains(actorId);
    }

    QueuedTurn prepare(String actorId) {
        return new QueuedTurn(
            actorId,
            queues.computeIfAbsent(actorId, ignored -> new ZLinkAsyncSerialQueue(false)));
    }

    void remove(String actorId) {
        queues.remove(actorId);
        activeActorIds.remove(actorId);
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
                turnOperation);
    }

    synchronized Optional<ZLinkAsyncSerialQueue.RelocationSeal> trySeal(
        String actorId) {
        ZLinkAsyncSerialQueue queue = queues.get(actorId);
        return queue == null
            ? Optional.empty()
            : queue.trySealRelocation();
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

    <T> CompletionStage<T> runTurn(
        String actorId,
        Supplier<CompletionStage<T>> operation) {
        try (systems.zlink.framework.runtime.internal.handlers
                 .ZLinkSuspendInvocationContext.Scope ignored =
                 systems.zlink.framework.runtime.internal.handlers
                     .ZLinkSuspendInvocationContext.enterActorDispatch(actorId)) {
            return operation.get();
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        }
    }

    record QueuedTurn(String actorId, ZLinkAsyncSerialQueue queue) {
    }
}
