package systems.zlink.framework;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class ZLinkSubmitStage extends CompletableFuture<Void> {
    private ZLinkSubmitStage() {
    }

    public static ZLinkSubmitStage completed() {
        ZLinkSubmitStage stage = new ZLinkSubmitStage();
        stage.complete(null);
        return stage;
    }

    public static ZLinkSubmitStage from(CompletionStage<Void> source) {
        ZLinkSubmitStage stage = new ZLinkSubmitStage();
        source.whenComplete((ignored, error) -> {
            if (error != null) {
                stage.completeExceptionally(error);
            } else {
                stage.complete(null);
            }
        });
        return stage;
    }

    public void await() {
        join();
    }
}
