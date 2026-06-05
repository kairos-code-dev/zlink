package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;

@ZLinkHandlerGroup("play-actor")
public final class PlayActorPlaceMarkHandler {
    @ZLinkSpotActorRequest
    public CompletionStage<PlaceMarkRes> placeMark(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        PlaceMarkReq request,
        CancellationToken cancellationToken) {
        actor.requireJoinedGame();
        return spot.placeMark(actor, request.cell());
    }
}
