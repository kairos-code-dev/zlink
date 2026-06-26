package systems.zlink.framework;

import java.util.concurrent.CompletionStage;

public final class ZLinkAwait {
    private ZLinkAwait() {
    }

    public static <T> T await(CompletionStage<T> stage) {
        return stage.toCompletableFuture().join();
    }

    public static void awaitVoid(CompletionStage<Void> stage) {
        stage.toCompletableFuture().join();
    }

}
