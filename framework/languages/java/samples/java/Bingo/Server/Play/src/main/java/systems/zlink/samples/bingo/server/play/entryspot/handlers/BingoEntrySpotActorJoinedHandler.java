package systems.zlink.samples.bingo.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotPostActorJoinedHandler;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.entryspot.BingoEntrySpot;

public final class BingoEntrySpotActorJoinedHandler
    implements ZLinkSpotPostActorJoinedHandler<BingoEntrySpot, PlayerActor> {
    @Override
    public CompletionStage<Void> handleAsync(
        BingoEntrySpot spot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult result,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }
}
