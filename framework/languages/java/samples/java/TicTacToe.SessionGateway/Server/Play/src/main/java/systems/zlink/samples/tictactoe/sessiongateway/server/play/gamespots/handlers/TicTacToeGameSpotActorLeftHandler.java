package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeMatchRoom;

public final class TicTacToeGameSpotActorLeftHandler {
    public TicTacToeMatchRoom handle(TicTacToeMatchRoom room, String actorId) {
        return new TicTacToeMatchRoom(room.matchId(), room.lastActorId().equals(actorId) ? "" : room.lastActorId());
    }
}
