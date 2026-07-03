package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.function.Supplier;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkWorkerFailedException;
import systems.zlink.framework.errors.ZLinkWorkerQueueFullException;
import systems.zlink.framework.errors.ZLinkWorkerTimeoutException;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.execution.ZLinkWorkerPool;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerStages;
import systems.zlink.framework.spots.ZLinkWorkerCall;
import systems.zlink.framework.spots.ZLinkWorkerTask;

/**
 * Runtime {@code runWorker(...)} call. The work runs on the shared elastic worker
 * pool; the awaitable terminator completes the returned stage, and the callback
 * terminator posts both callbacks to the owning Spot serial line. A late result
 * after a timeout is dropped without invoking user callbacks.
 */
final class DefaultZLinkWorkerCall<T> implements ZLinkWorkerCall<T> {
    private final ZLinkWorkerPool pool;
    private final ZLinkWorkerTask<T> work;
    private final Function<Supplier<CompletionStage<Void>>, CompletionStage<Void>> postToSpotQueue;
    private final boolean captureCurrentTurn;
    private final ZLinkYieldTurn turn;
    private final AtomicBoolean terminated = new AtomicBoolean();
    private Duration timeout;

    DefaultZLinkWorkerCall(
        ZLinkWorkerPool pool,
        ZLinkWorkerTask<T> work,
        Function<Supplier<CompletionStage<Void>>, CompletionStage<Void>> postToSpotQueue) {
        this(pool, work, postToSpotQueue, true);
    }

    DefaultZLinkWorkerCall(
        ZLinkWorkerPool pool,
        ZLinkWorkerTask<T> work,
        Function<Supplier<CompletionStage<Void>>, CompletionStage<Void>> postToSpotQueue,
        boolean captureCurrentTurn) {
        this.pool = Objects.requireNonNull(pool, "pool");
        this.work = Objects.requireNonNull(work, "work");
        this.postToSpotQueue = Objects.requireNonNull(postToSpotQueue, "postToSpotQueue");
        this.captureCurrentTurn = captureCurrentTurn;
        this.turn = captureCurrentTurn ? ZLinkFrameworkTurns.captureCurrent() : null;
    }

    @Override
    public ZLinkWorkerCall<T> timeout(Duration timeout) {
        if (timeout == null || timeout.isNegative() || timeout.isZero()) {
            throw new ZLinkConfigurationException("worker timeout must be positive");
        }
        this.timeout = timeout;
        return this;
    }

    @Override
    public CompletionStage<T> submit() {
        ensureSingleTerminator();
        CompletableFuture<T> result = new CompletableFuture<>();
        start(result::complete, result::completeExceptionally, () -> false);
        return result;
    }

    @Override
    public T yield() {
        ZLinkYieldTurn capturedTurn = requireTurn();
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(capturedTurn, submit()));
    }

    @Override
    public T yield(CancellationToken cancellationToken) {
        ZLinkFrameworkTurns.throwIfCancellationRequested(cancellationToken);
        ZLinkYieldTurn capturedTurn = requireTurn();
        CompletionStage<T> result = submit(cancellationToken);
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(capturedTurn, result, cancellationToken));
    }

    @Override
    public void submit(
        BiConsumer<T, CancellationToken> onCompleted,
        BiConsumer<Throwable, CancellationToken> onError) {
        Objects.requireNonNull(onCompleted, "onCompleted");
        Objects.requireNonNull(onError, "onError");
        ensureSingleTerminator();
        AtomicBoolean cancelledView = new AtomicBoolean();
        CancellationToken callbackToken = cancelledView::get;
        start(
            value -> postToSpotQueue.apply(() ->
                ZLinkHandlerStages.fromRunnable(() -> onCompleted.accept(value, callbackToken))),
            error -> postToSpotQueue.apply(() ->
                ZLinkHandlerStages.fromRunnable(() -> onError.accept(error, callbackToken))),
            () -> false);
    }

    private CompletionStage<T> submit(CancellationToken cancellationToken) {
        ensureSingleTerminator();
        CompletableFuture<T> result = new CompletableFuture<>();
        start(result::complete, result::completeExceptionally, cancellationToken);
        return result;
    }

    private void start(
        Consumer<T> complete,
        Consumer<Throwable> fail,
        CancellationToken cancellationToken) {
        Objects.requireNonNull(cancellationToken, "cancellationToken");
        AtomicBoolean settled = new AtomicBoolean();
        AtomicBoolean timeoutCancelled = new AtomicBoolean();
        CancellationToken token = () ->
            timeoutCancelled.get() || cancellationToken.isCancellationRequested();
        ScheduledFuture<?> timeoutFuture = timeout == null
            ? null
            : pool.scheduleTimeout(() -> {
                timeoutCancelled.set(true);
                if (settled.compareAndSet(false, true)) {
                    fail.accept(new ZLinkWorkerTimeoutException(
                        "worker call timed out after " + timeout));
                }
            }, timeout);
        try {
            pool.execute(() -> {
                T value;
                try {
                    value = work.run(token);
                } catch (Exception ex) {
                    if (settled.compareAndSet(false, true)) {
                        cancelTimeout(timeoutFuture);
                        fail.accept(new ZLinkWorkerFailedException("worker call failed", ex));
                    }
                    return;
                }
                // Late completion after a timeout: the settle flag is already
                // taken, so the result is dropped without user callbacks.
                if (settled.compareAndSet(false, true)) {
                    cancelTimeout(timeoutFuture);
                    complete.accept(value);
                }
            });
        } catch (RejectedExecutionException ex) {
            cancelTimeout(timeoutFuture);
            if (settled.compareAndSet(false, true)) {
                fail.accept(new ZLinkWorkerQueueFullException("worker queue is full"));
            }
        }
    }

    private void ensureSingleTerminator() {
        if (!terminated.compareAndSet(false, true)) {
            throw new IllegalStateException(
                "runWorker call already has a terminator; call submit once");
        }
    }

    private ZLinkYieldTurn requireTurn() {
        if (turn == null) {
            if (captureCurrentTurn) {
                ZLinkYieldTurn current = ZLinkFrameworkTurns.captureCurrent();
                if (current != null) {
                    return current;
                }
            }
            throw new IllegalStateException(
                "yield requires a framework Spot handler turn captured when the call object was created");
        }
        return turn;
    }

    private static void cancelTimeout(ScheduledFuture<?> timeoutFuture) {
        if (timeoutFuture != null) {
            timeoutFuture.cancel(false);
        }
    }
}
