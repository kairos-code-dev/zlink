package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomCreated

class BingoRoomSpotCreatedHandler {
    fun handle(roomId: String): BingoRoomCreated =
        BingoRoomCreated(roomId)
}
