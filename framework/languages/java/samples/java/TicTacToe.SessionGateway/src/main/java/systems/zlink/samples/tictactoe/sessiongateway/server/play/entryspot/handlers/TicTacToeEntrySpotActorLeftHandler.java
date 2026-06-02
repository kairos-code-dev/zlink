package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers;

public final class TicTacToeEntrySpotActorLeftHandler {
    public String handle(String actorId, String matchId) {
        return actorId + " left entry spot for " + matchId;
    }
}
