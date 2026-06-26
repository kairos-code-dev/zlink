package systems.zlink.framework.execution;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiFunction;

public final class ZLinkYieldTurn {
    private static final ThreadLocal<ZLinkYieldTurn> CURRENT = new ThreadLocal<>();

    private final BiFunction<ZLinkYieldTurn, Runnable, Boolean> postResume;
    private final AtomicBoolean suspendSignaled = new AtomicBoolean();
    private volatile CompletableFuture<Void> suspended = new CompletableFuture<>();
    private volatile CompletableFuture<Void> owner;

    ZLinkYieldTurn(BiFunction<ZLinkYieldTurn, Runnable, Boolean> postResume) {
        this.postResume = Objects.requireNonNull(postResume, "postResume");
    }

    static ZLinkYieldTurn current() {
        return CURRENT.get();
    }

    public CompletableFuture<Void> suspended() {
        return suspended;
    }

    public CompletableFuture<Void> owner() {
        return owner;
    }

    public void bindOwner(CompletableFuture<Void> owner) {
        this.owner = Objects.requireNonNull(owner, "owner");
    }

    public void resetSuspension() {
        suspended = new CompletableFuture<>();
        suspendSignaled.set(false);
    }

    <T> CompletionStage<T> awaitFrameworkCall(CompletionStage<T> stage) {
        CompletableFuture<T> operation = stage.toCompletableFuture();
        if (operation.isDone()) {
            return operation;
        }
        signalSuspended();
        return operation.thenCompose(value ->
            awaitResumePermit().thenApply(ignored -> value));
    }

    <T> T awaitFrameworkCallBlocking(CompletionStage<T> stage) {
        return awaitFrameworkCall(stage).toCompletableFuture().join();
    }

    void awaitFrameworkCallVoid(CompletionStage<Void> stage) {
        awaitFrameworkCallBlocking(stage);
    }

    Scope push() {
        ZLinkYieldTurn previous = CURRENT.get();
        CURRENT.set(this);
        return new Scope(previous);
    }

    private void signalSuspended() {
        if (suspendSignaled.compareAndSet(false, true)) {
            suspended.complete(null);
        }
    }

    private CompletableFuture<Void> awaitResumePermit() {
        CompletableFuture<Void> resume = new CompletableFuture<>();
        if (!postResume.apply(this, () -> resume.complete(null))) {
            resume.completeExceptionally(new IllegalStateException(
                "ZLink serial execution queue is closed"));
        }
        return resume;
    }

    static final class Scope implements AutoCloseable {
        private final ZLinkYieldTurn previous;

        Scope(ZLinkYieldTurn previous) {
            this.previous = previous;
        }

        @Override
        public void close() {
            if (previous == null) {
                CURRENT.remove();
            } else {
                CURRENT.set(previous);
            }
        }
    }
}
