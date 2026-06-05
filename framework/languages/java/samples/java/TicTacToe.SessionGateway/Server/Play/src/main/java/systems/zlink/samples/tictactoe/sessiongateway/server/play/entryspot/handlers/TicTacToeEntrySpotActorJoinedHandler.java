package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot;
import systems.zlink.samples.tictactoe.sessiongateway.shared.actors.PlayerActor;

public final class TicTacToeEntrySpotActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    public CompletionStage<Void> actorJoined(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken) {
        return CompletableFuture.completedFuture(null);
    }
}
