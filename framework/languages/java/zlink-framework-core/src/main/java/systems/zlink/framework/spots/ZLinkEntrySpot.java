package systems.zlink.framework.spots;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public interface ZLinkEntrySpot {
    ZLinkEntrySpotContext context();

    default void configure() {
    }

    default CompletionStage<Void> onInitializeAsync() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onClosingAsync() {
        return CompletableFuture.completedFuture(null);
    }
}
