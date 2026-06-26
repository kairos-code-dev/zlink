package systems.zlink.framework.execution;

import java.util.concurrent.CompletionStage;

public final class ZLinkYieldTurnTestSupport {
    private ZLinkYieldTurnTestSupport() {
    }

    public static <T> T awaitInCurrentTurn(CompletionStage<T> stage) {
        ZLinkYieldTurn turn = ZLinkYieldTurn.current();
        if (turn == null) {
            throw new IllegalStateException("missing framework turn");
        }
        return turn.awaitFrameworkCallBlocking(stage);
    }
}
