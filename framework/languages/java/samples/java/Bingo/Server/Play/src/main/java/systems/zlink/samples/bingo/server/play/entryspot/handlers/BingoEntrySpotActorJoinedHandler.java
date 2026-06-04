package systems.zlink.samples.bingo.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;

public final class BingoEntrySpotActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    public CompletionStage<Void> handleAsync(
        PlayerActor actor,
        ZLinkSpotActorChangeResult result) {
        return CompletableFuture.completedFuture(null);
    }
}
