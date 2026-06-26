package systems.zlink.framework.execution;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.Objects;
import java.util.function.Supplier;

public final class ZLinkAsyncSerialQueue {
    private static final ExecutorService HANDLER_EXECUTOR =
        Executors.newVirtualThreadPerTaskExecutor();

    private CompletionStage<Void> tail = CompletableFuture.completedFuture(null);

    public synchronized CompletionStage<Void> enqueue(Supplier<CompletionStage<Void>> operation) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        CompletionStage<Void> gate = tail
            .handle((ignored, error) -> null)
            .thenCompose(ignored -> invokeTurn(operation, result));
        tail = gate.handle((ignored, error) -> null);
        return result;
    }

    private CompletionStage<Void> enqueueResume(ZLinkYieldTurn turn, Runnable resume) {
        CompletionStage<Void> scheduled;
        synchronized (this) {
            scheduled = tail
                .handle((ignored, error) -> null)
                .thenCompose(ignored -> invokeResume(turn, resume));
            tail = scheduled.handle((ignored, error) -> null);
        }
        return scheduled;
    }

    private CompletionStage<Void> invokeTurn(
        Supplier<CompletionStage<Void>> operation,
        CompletableFuture<Void> result) {
        ZLinkYieldTurn turn = new ZLinkYieldTurn((yieldTurn, resume) -> {
            enqueueResume(yieldTurn, resume);
            return true;
        });
        CompletableFuture<Void> owner = CompletableFuture.runAsync(() -> {
            try (ZLinkYieldTurn.Scope ignored = turn.push()) {
                Objects.requireNonNull(operation.get(), "operation result")
                    .toCompletableFuture()
                    .join();
            }
        }, HANDLER_EXECUTOR);
        turn.bindOwner(owner);
        owner.whenComplete((value, error) -> {
            if (error != null) {
                result.completeExceptionally(error);
            } else {
                result.complete(null);
            }
        });
        CompletableFuture<Void> suspended = turn.suspended();
        return firstOf(owner, suspended).thenCompose(ignored -> {
            if (suspended.isDone() && !owner.isDone()) {
                return CompletableFuture.completedFuture(null);
            }
            return owner;
        });
    }

    private CompletionStage<Void> invokeResume(ZLinkYieldTurn turn, Runnable resume) {
        turn.resetSuspension();
        CompletableFuture<Void> owner = turn.owner();
        CompletableFuture<Void> suspended = turn.suspended();
        CompletableFuture<Void> resumed = CompletableFuture.runAsync(() -> {
            resume.run();
            if (owner != null && !owner.isDone()) {
                firstOf(owner, suspended).join();
            }
        }, HANDLER_EXECUTOR);
        return resumed;
    }

    private static CompletableFuture<Void> firstOf(
        CompletableFuture<Void> first,
        CompletableFuture<Void> second) {
        return CompletableFuture.anyOf(first, second).thenApply(ignored -> null);
    }
}
