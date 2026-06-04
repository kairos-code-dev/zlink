package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomModels;

public final class BingoRoomActorLeftHandler {
    public BingoRoomModels.BingoRoomMembership handle(String roomId, String actorId) {
        return new BingoRoomModels.BingoRoomMembership(roomId, actorId, false);
    }
}
