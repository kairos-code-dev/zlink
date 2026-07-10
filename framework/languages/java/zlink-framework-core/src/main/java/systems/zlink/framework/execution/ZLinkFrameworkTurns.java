package systems.zlink.framework.execution;

import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import java.util.Set;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.errors.ZLinkOperationCanceledException;

public final class ZLinkFrameworkTurns {
    private static final ScheduledExecutorService CANCELLATION_WATCHER =
        Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-framework-cancellation-watcher");
            thread.setDaemon(true);
            return thread;
        });

    private static final Set<String> FRAMEWORK_CALLERS = Set.of(
        "systems.zlink.framework.runtime.actors.ZLinkActorRuntime",
        "systems.zlink.framework.runtime.actors.ZLinkActorEntrySpotJoinCall",
        "systems.zlink.framework.runtime.actors.ZLinkActorSpotJoinCall",
        "systems.zlink.framework.runtime.actors.ZLinkBoundSessionRuntime",
        "systems.zlink.framework.runtime.actors.ZLinkNativeBoundSessionRuntime",
        "systems.zlink.framework.runtime.actors.ZLinkRoutedBoundSessionRuntime",
        "systems.zlink.framework.runtime.channels.ZLinkChannelRuntime",
        "systems.zlink.framework.runtime.channels.ZLinkChannelHandlerInvoker",
        "systems.zlink.framework.runtime.channels.RequestCall",
        "systems.zlink.framework.runtime.channels.RouteRequestCall",
        "systems.zlink.framework.runtime.channels.RouteSpotRequestCall",
        "systems.zlink.framework.runtime.spots.DefaultZLinkWorkerCall",
        "systems.zlink.framework.runtime.spots.ZLinkSpotDirectOutbound",
        "systems.zlink.framework.runtime.spots.ZLinkSpotDirectRequestCall",
        "systems.zlink.framework.runtime.spots.ZLinkSpotRoutedOutbound",
        "systems.zlink.framework.runtime.spots.ZLinkSpotRoutedRequestCall",
        "systems.zlink.framework.runtime.spots.ZLinkSpotRuntime",
        "systems.zlink.framework.kotlin.ZLinkCoroutineTurnAwaitKt",
        "systems.zlink.framework.kotlin.ZLinkCoroutineSuspendHandlerInvoker");

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
        var watcher = CANCELLATION_WATCHER.scheduleWithFixedDelay(() -> {
            if (cancellationToken.isCancellationRequested()) {
                result.completeExceptionally(new ZLinkOperationCanceledException(
                    "operation was canceled"));
            }
        }, 0, 10, TimeUnit.MILLISECONDS);
        operation.whenComplete((value, error) -> {
            if (error != null) {
                result.completeExceptionally(error);
            } else {
                result.complete(value);
            }
        });
        result.whenComplete((ignored, error) -> watcher.cancel(false));
        return result;
    }

    private static void requireNotCanceled(CancellationToken cancellationToken) {
        Objects.requireNonNull(cancellationToken, "cancellationToken");
        if (cancellationToken.isCancellationRequested()) {
            throw new ZLinkOperationCanceledException("operation was canceled");
        }
    }

    private static void requireFrameworkCaller() {
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        for (int i = 2; i < stack.length; i++) {
            String className = stack[i].getClassName();
            if (className.equals(ZLinkFrameworkTurns.class.getName())) {
                continue;
            }
            if (isFrameworkCaller(className)) {
                return;
            }
            break;
        }
        throw new IllegalStateException(
            "ZLink framework turn helpers are only available to framework-managed call objects");
    }

    private static boolean isFrameworkCaller(String className) {
        for (String frameworkCaller : FRAMEWORK_CALLERS) {
            if (className.equals(frameworkCaller)
                || className.startsWith(frameworkCaller + "$")) {
                return true;
            }
        }
        return false;
    }
}
