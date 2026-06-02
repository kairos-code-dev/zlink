package systems.zlink.samples.bingo.server.play.handlers;

public final class AllocateBingoRoomHandler {
    public String allocate(String playerId) {
        return "room-1:" + playerId;
    }
}
