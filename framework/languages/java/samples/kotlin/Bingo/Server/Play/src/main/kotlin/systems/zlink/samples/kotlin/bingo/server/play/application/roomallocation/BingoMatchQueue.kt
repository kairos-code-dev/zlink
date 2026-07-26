package systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation

interface BingoMatchQueue {
    fun reserve(
        mode: String,
        actorId: String,
        newRoomId: String,
        requiredPlayers: Int,
    ): BingoMatchReservation
}
