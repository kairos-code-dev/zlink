package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameCatalog;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;

public final class PlayActorPlaceMarkHandler {
    public PlaceMarkRes placeMark(String gameId, String actorId, int cell) {
        return TicTacToeGameCatalog.get(gameId).placeMark(actorId, cell);
    }
}
