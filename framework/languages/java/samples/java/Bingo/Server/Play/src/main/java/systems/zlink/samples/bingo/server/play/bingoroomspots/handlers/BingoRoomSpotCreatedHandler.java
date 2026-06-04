package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomModels;

public final class BingoRoomSpotCreatedHandler {
    public BingoRoomModels.BingoRoomCreated handle(String roomId) {
        return new BingoRoomModels.BingoRoomCreated(roomId);
    }
}
