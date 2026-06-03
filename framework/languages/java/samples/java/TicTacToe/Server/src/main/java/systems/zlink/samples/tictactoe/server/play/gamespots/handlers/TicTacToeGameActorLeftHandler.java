package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorLeft;
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;

public final class TicTacToeGameActorLeftHandler {
    @ZLinkSpotActorLeft
    public CompletionStage<Void> actorLeft(
        PlayActor actor,
        ZLinkSpotActorChangeResult info) {
        return CompletableFuture.completedFuture(null);
    }
}
