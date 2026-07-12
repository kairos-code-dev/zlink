package systems.zlink.framework.execution;

import java.util.Objects;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;

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
