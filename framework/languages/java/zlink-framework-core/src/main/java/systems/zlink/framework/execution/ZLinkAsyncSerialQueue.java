package systems.zlink.framework.execution;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Supplier;
import systems.zlink.framework.runtime.internal.diagnostics.ZLinkFlowContext;

public final class ZLinkAsyncSerialQueue {
    private static final ExecutorService HANDLER_EXECUTOR =
        Executors.newVirtualThreadPerTaskExecutor();
    private static final ThreadLocal<ZLinkAsyncSerialQueue> CURRENT = new ThreadLocal<>();
    private static final ThreadLocal<CompletableFuture<Void>> CURRENT_GATE = new ThreadLocal<>();
    private static final ThreadLocal<Boolean> CURRENT_RELEASE_DEFERRED = new ThreadLocal<>();

    private final boolean releaseOnIncompleteStage;
    private CompletionStage<Void> tail = CompletableFuture.completedFuture(null);

    public ZLinkAsyncSerialQueue() {
        this(false);
    }

    public ZLinkAsyncSerialQueue(boolean releaseOnIncompleteStage) {
        this.releaseOnIncompleteStage = releaseOnIncompleteStage;
    }

    public synchronized CompletionStage<Void> enqueue(Supplier<CompletionStage<Void>> operation) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        ZLinkFlowContext.State flow = ZLinkFlowContext.current();
        CompletionStage<Void> gate = tail
            .handle((ignored, error) -> null)
            .thenCompose(ignored -> invoke(operation, result, flow));
        tail = gate.handle((ignored, error) -> null);
        return result;
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
        if (queue == null || !queue.releaseOnIncompleteStage) {
            return stage;
        }
        ZLinkFlowContext.State flow = ZLinkFlowContext.current();
        CompletableFuture<T> managed = new CompletableFuture<>();
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
}
