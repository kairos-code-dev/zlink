package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorLeft;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActor;

public final class TicTacToeEntrySpotActorLeftHandler {
    @ZLinkSpotActorLeft
    public CompletionStage<Void> actorLeft(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }
}
