package systems.zlink.samples.tictactoe.server.play.entryspot.handlers;

import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

public final class PlayActorJoinGameHandler {
    public JoinGameRes joinGame(String gameId, String actorId) {
        return TicTacToeGameDirectory.get(gameId).join(actorId);
    }
}
