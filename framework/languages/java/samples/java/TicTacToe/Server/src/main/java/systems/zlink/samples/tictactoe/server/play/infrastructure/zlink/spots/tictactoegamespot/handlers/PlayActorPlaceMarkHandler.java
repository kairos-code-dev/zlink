package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;

@ZLinkHandlerGroup("play-actor")
public final class PlayActorPlaceMarkHandler {
    @ZLinkSpotActorRequest
    public java.util.concurrent.CompletionStage<PlaceMarkRes> placeMark(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        PlaceMarkReq request) {
        actor.requireJoinedGame();
        PlaceMarkRes response = spot.placeMark(actor, request.cell());
        return java.util.concurrent.CompletableFuture.completedFuture(response);
    }
}
