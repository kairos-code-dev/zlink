package systems.zlink.samples.kotlin.bingo.server.play.handlers

class AllocateBingoRoomHandler {
    fun allocate(playerId: String): String = "room-1:$playerId"
}
