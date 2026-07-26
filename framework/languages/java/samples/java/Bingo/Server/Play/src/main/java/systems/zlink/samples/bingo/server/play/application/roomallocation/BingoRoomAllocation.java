package systems.zlink.samples.bingo.server.play.application.roomallocation;

import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;

public record BingoRoomAllocation(
    String roomId,
    BingoRoomModels.BingoRoomSettings settings) {
}
