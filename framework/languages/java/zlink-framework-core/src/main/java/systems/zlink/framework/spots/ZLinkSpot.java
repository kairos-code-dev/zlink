package systems.zlink.framework.spots;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.contracts.messaging.Message;

public interface ZLinkSpot {
    ZLinkSpotContext context();

    default void configure() {
    }

    default CompletionStage<ZLinkSpotCreateResponse> onCreateAsync(Message request) {
        return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
    }

    default CompletionStage<Void> onInitializeAsync() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<Void> onClosingAsync() {
        return CompletableFuture.completedFuture(null);
    }

    default CompletionStage<ZLinkSpotActorJoinResponse> onActorJoinAsync(
        ZLinkActor actor,
        Message request,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.reject());
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
