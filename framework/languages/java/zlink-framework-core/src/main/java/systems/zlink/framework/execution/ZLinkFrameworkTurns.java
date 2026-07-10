package systems.zlink.framework.execution;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.errors.ZLinkOperationCanceledException;

public final class ZLinkFrameworkTurns {
    private static final StackWalker CALLERS =
        StackWalker.getInstance(StackWalker.Option.RETAIN_CLASS_REFERENCE);

    private ZLinkFrameworkTurns() {
    }

    public static ZLinkYieldTurn captureCurrent() {
        requireFrameworkCaller();
        return ZLinkYieldTurn.current();
    }

    public static <T> T runWithTurn(ZLinkYieldTurn turn, Supplier<T> action) {
        requireFrameworkCaller();
        Objects.requireNonNull(action, "action");
        if (turn == null) {
            return action.get();
        }
        try (ZLinkYieldTurn.Scope ignored = turn.push()) {
            return action.get();
        }
    }

    public static AutoCloseable enterTurn(ZLinkYieldTurn turn) {
        requireFrameworkCaller();
        if (turn == null) {
            return () -> {};
        }
        return turn.push();
    }

    public static <T> CompletionStage<T> awaitManagedCompletion(
        ZLinkYieldTurn turn,
        CompletionStage<T> stage) {
        requireFrameworkCaller();
        return turn.awaitFrameworkCall(stage);
    }

    public static <T> CompletionStage<T> awaitManagedCompletion(
        ZLinkYieldTurn turn,
        CompletionStage<T> stage,
        CancellationToken cancellationToken) {
        requireFrameworkCaller();
        return turn.awaitFrameworkCall(cancellable(stage, cancellationToken));
    }

    public static void throwIfCancellationRequested(CancellationToken cancellationToken) {
        requireFrameworkCaller();
        requireNotCanceled(cancellationToken);
    }

    private static <T> CompletionStage<T> cancellable(
        CompletionStage<T> stage,
        CancellationToken cancellationToken) {
        Objects.requireNonNull(stage, "stage");
        requireNotCanceled(cancellationToken);
        CompletableFuture<T> operation = stage.toCompletableFuture();
        if (operation.isDone()) {
            return operation;
        }
        CompletableFuture<T> result = new CompletableFuture<>();
        class CancellationCheck implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                if (cancellationToken.isCancellationRequested()) {
                    result.completeExceptionally(new ZLinkOperationCanceledException(
                        "operation was canceled"));
                    return;
                }
                CompletableFuture.delayedExecutor(10, TimeUnit.MILLISECONDS).execute(this);
            }
        }
        new CancellationCheck().run();
        operation.whenComplete((value, error) -> {
            if (error != null) {
                result.completeExceptionally(error);
            } else {
                result.complete(value);
            }
        });
        return result;
    }

    private static void requireNotCanceled(CancellationToken cancellationToken) {
        Objects.requireNonNull(cancellationToken, "cancellationToken");
        if (cancellationToken.isCancellationRequested()) {
            throw new ZLinkOperationCanceledException("operation was canceled");
        }
    }

    private static void requireFrameworkCaller() {
        boolean frameworkCaller = CALLERS.walk(frames -> frames
            .dropWhile(frame -> frame.getDeclaringClass() == ZLinkFrameworkTurns.class)
            .findFirst()
            .map(StackWalker.StackFrame::getDeclaringClass)
            .map(Class::getPackageName)
            .filter(ZLinkFrameworkTurns::isFrameworkPackage)
            .isPresent());
        if (!frameworkCaller) {
            throw new IllegalStateException(
                "ZLink framework turn helpers are only available to framework-managed call objects");
        }
    }

    private static boolean isFrameworkPackage(String packageName) {
        return packageName.startsWith("systems.zlink.framework.runtime.")
            || packageName.equals("systems.zlink.framework.kotlin");
    }
}
