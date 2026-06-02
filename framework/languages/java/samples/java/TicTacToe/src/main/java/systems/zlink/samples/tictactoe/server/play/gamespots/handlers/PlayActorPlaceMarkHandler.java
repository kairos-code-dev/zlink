package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.PlaceMarkRes;

public final class PlayActorPlaceMarkHandler {
    public PlaceMarkRes placeMark(String gameId, String actorId, int cell) {
        return TicTacToeGameDirectory.get(gameId).placeMark(actorId, cell);
    }
}
