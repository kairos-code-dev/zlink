package systems.zlink.framework.execution;

import java.util.concurrent.CompletionStage;
import java.util.Set;

public final class ZLinkFrameworkTurns {
    private static final Set<String> FRAMEWORK_CALLERS = Set.of(
        "systems.zlink.framework.runtime.actors.ZLinkActorRuntime",
        "systems.zlink.framework.runtime.actors.ZLinkBoundSessionRuntime",
        "systems.zlink.framework.runtime.actors.ZLinkNativeBoundSessionRuntime",
        "systems.zlink.framework.runtime.actors.ZLinkRoutedBoundSessionRuntime",
        "systems.zlink.framework.runtime.channels.ZLinkChannelRuntime",
        "systems.zlink.framework.runtime.spots.DefaultZLinkWorkerCall",
        "systems.zlink.framework.runtime.spots.ZLinkSpotRuntime");

    private ZLinkFrameworkTurns() {
    }

    public static ZLinkYieldTurn captureCurrent() {
        requireFrameworkCaller();
        return ZLinkYieldTurn.current();
    }

    public static <T> CompletionStage<T> awaitManagedCompletion(
        ZLinkYieldTurn turn,
        CompletionStage<T> stage) {
        requireFrameworkCaller();
        return turn.awaitFrameworkCall(stage);
    }

    private static void requireFrameworkCaller() {
        StackTraceElement[] stack = Thread.currentThread().getStackTrace();
        for (int i = 2; i < stack.length; i++) {
            String className = stack[i].getClassName();
            if (className.equals(ZLinkFrameworkTurns.class.getName())) {
                continue;
            }
            if (FRAMEWORK_CALLERS.contains(className)) {
                return;
            }
            break;
        }
        throw new IllegalStateException(
            "ZLink framework turn helpers are only available to framework-managed call objects");
    }
}
