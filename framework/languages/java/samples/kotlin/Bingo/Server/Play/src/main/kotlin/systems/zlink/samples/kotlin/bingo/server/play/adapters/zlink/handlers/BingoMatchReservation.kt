package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.handlers

data class BingoMatchReservation(
    val roomId: String,
    val ownerPlayNodeRid: String,
)
