package systems.zlink.samples.tictactoe.server.play.entryspot;

import systems.zlink.samples.tictactoe.server.play.entryspot.handlers.PlayActorJoinGameHandler;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

public final class PlayEntrySpot {
    private final PlayActorJoinGameHandler joinGameHandler = new PlayActorJoinGameHandler();

    public JoinGameRes joinGame(String gameId, String actorId) {
        return joinGameHandler.joinGame(gameId, actorId);
    }
}
