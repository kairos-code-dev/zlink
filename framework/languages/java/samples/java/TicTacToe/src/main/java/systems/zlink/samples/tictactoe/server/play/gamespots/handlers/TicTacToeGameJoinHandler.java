package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

public final class TicTacToeGameJoinHandler {
    public JoinGameRes join(String gameId, String actorId) {
        return TicTacToeGameDirectory.get(gameId).join(actorId);
    }
}
