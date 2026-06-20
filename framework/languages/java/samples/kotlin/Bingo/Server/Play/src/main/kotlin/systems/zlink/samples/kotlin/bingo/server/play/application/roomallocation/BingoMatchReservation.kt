package systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation

data class BingoMatchReservation(
    val roomId: String,
    val ownerPlayNodeRid: String,
)
