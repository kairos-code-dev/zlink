package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;

@ZLinkHandlerGroup("play-actor")
public final class PlayActorPlaceMarkHandler {
    @ZLinkSpotActorRequest
    public CompletionStage<PlaceMarkRes> placeMark(
        PlayActor actor,
        PlaceMarkReq request) {
        actor.requireJoinedGame();
        return actor.context()
            .getSpot(TicTacToeGame.class)
            .placeMark(actor, request.cell());
    }
}
