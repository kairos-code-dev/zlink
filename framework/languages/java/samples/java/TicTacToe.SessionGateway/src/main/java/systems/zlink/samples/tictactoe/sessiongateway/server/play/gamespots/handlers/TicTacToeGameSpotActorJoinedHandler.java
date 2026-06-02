package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeMatchRoom;

public final class TicTacToeGameSpotActorJoinedHandler {
    public TicTacToeMatchRoom handle(TicTacToeMatchRoom room, String actorId) {
        return new TicTacToeMatchRoom(room.matchId(), actorId);
    }
}
