package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkReq;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;

@ZLinkHandlerGroup("play-actor")
public final class PlayActorPlaceMarkHandler {
    public PlaceMarkRes placeMark(String gameId, String actorId, int cell) {
        return TicTacToeGameDirectory.get(gameId).placeMark(actorId, cell);
    }

    @ZLinkSpotActorRequest
    public PlaceMarkRes placeMark(
        PlayActor actor,
        PlaceMarkReq request) {
        return TicTacToeGameDirectory.get(actor.gameId())
            .placeMark(actor.actorId(), request.cell());
    }
}
