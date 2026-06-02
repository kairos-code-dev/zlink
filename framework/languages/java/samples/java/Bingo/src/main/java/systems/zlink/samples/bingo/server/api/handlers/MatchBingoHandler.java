package systems.zlink.samples.bingo.server.api.handlers;

public final class MatchBingoHandler {
    public String match(String playerId) {
        return "room-1:" + playerId;
    }
}
