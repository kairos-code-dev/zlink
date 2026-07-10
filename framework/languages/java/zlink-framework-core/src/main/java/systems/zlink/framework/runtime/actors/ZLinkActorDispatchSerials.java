package systems.zlink.framework.runtime.actors;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;

final class ZLinkActorDispatchSerials {
    private final Map<String, ZLinkAsyncSerialQueue> queues = new HashMap<>();
    private final Set<String> activeActorIds = new HashSet<>();
    private final ThreadLocal<String> currentActorId = new ThreadLocal<>();

    boolean isCurrent(String actorId) {
        return actorId.equals(currentActorId.get());
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
        return turn.queue.enqueue(() -> {
            synchronized (this) {
                activeActorIds.add(turn.actorId);
            }
            return runTurn(turn.actorId, operation)
                .whenComplete((ignored, error) -> {
                    synchronized (this) {
                        activeActorIds.remove(turn.actorId);
                    }
                });
        });
    }

    <T> CompletionStage<T> runTurn(
        String actorId,
        Supplier<CompletionStage<T>> operation) {
        String previous = currentActorId.get();
        currentActorId.set(actorId);
        try {
            return operation.get();
        } catch (RuntimeException ex) {
            return CompletableFuture.failedFuture(ex);
        } finally {
            if (previous == null) {
                currentActorId.remove();
            } else {
                currentActorId.set(previous);
            }
        }
    }

    record QueuedTurn(String actorId, ZLinkAsyncSerialQueue queue) {
    }
}
