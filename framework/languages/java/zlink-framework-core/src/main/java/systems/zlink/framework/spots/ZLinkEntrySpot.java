package systems.zlink.framework.spots;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;

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

    default CompletionStage<Void> onPostActorJoinedAsync(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onActorLeftAsync(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }
}
