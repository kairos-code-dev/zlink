package systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation

import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings

data class BingoRoomAllocation(
    val roomId: String,
    val settings: BingoRoomSettings,
)
