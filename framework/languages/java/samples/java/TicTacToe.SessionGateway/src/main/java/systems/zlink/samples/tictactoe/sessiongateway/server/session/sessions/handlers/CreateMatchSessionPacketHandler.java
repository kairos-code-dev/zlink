package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions.handlers;

public final class CreateMatchSessionPacketHandler {
    public String create(String actorId) {
        return "match-" + actorId;
    }
}
