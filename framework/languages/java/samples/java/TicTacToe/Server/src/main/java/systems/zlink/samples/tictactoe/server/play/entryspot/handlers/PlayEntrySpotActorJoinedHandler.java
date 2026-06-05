package systems.zlink.samples.tictactoe.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.entryspot.PlayEntrySpot;

public final class PlayEntrySpotActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    public CompletionStage<Void> actorJoined(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }
}
