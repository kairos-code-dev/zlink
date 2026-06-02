package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomMembership

class BingoRoomActorLeftHandler {
    fun handle(roomId: String, actorId: String): BingoRoomMembership =
        BingoRoomMembership(roomId, actorId, joined = false)
}
