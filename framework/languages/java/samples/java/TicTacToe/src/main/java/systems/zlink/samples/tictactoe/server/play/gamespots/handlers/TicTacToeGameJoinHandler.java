package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameCatalog;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

public final class TicTacToeGameJoinHandler {
    public JoinGameRes join(String gameId, String actorId) {
        return TicTacToeGameCatalog.get(gameId).join(actorId);
    }
}
