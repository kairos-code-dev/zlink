package systems.zlink.samples.bingo.client;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class SampleAsync {
    private SampleAsync() {
    }

    public static <T> T await(CompletionStage<T> stage) throws Exception {
        CompletableFuture<T> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
        });
        return done.get();
    }
}
