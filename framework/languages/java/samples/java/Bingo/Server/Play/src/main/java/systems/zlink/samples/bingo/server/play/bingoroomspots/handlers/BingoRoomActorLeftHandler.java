package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorLeftHandler;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;

public final class BingoRoomActorLeftHandler
    implements ZLinkSpotActorLeftHandler<BingoRoomSpot, PlayerActor> {
    @Override
    public CompletionStage<Void> handleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult result,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }
}
