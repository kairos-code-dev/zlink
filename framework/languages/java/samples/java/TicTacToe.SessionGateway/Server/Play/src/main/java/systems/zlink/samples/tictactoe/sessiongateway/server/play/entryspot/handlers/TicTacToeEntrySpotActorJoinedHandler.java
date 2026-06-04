package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers;

public final class TicTacToeEntrySpotActorJoinedHandler {
    public String handle(String actorId, String matchId) {
        return actorId + " joined entry spot for " + matchId;
    }
}
