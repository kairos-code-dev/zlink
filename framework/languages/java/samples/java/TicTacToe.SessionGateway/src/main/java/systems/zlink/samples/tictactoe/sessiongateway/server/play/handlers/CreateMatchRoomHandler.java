package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

public final class CreateMatchRoomHandler {
    public String create(String actorId) {
        return "match-" + actorId;
    }
}
