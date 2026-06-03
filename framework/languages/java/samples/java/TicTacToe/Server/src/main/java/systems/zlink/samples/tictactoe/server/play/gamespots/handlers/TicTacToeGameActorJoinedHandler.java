package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;

public final class TicTacToeGameActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    public CompletionStage<Void> actorJoined(
        PlayActor actor,
        ZLinkSpotActorChangeResult info) {
        return CompletableFuture.completedFuture(null);
    }
}
