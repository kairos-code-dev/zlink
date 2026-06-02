package systems.zlink.framework.spots;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSpot {
    ZLinkSpotContext context();

    default void configure() {
    }

    default CompletionStage<Void> onCreateAsync(List<Message> createParts) {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onInitializeAsync() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onClosingAsync() {
        return CompletableFuture.completedFuture(null);
    }
}
