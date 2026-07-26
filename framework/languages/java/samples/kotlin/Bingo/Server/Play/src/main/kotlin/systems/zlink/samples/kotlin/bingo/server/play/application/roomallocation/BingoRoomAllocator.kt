package systems.zlink.samples.kotlin.bingo.server.play.application.roomallocation

import systems.zlink.samples.kotlin.bingo.server.play.domain.bingo.BingoRoomSettings

class BingoRoomAllocator(
    private val matchQueue: BingoMatchQueue,
    private val drawPeriodMillis: Long,
) {
    private var roomSeq: Int = 0

    suspend fun allocate(
        actorId: String,
        mode: String,
    ): BingoRoomAllocation {
        if (actorId.isBlank()) {
            throw IllegalStateException("actorId is required")
        }
        if (mode != "two-player") {
            throw IllegalStateException("Unsupported bingo mode. mode=$mode")
        }

        val settings = BingoRoomSettings.create(mode, nextRoomSeq(), drawPeriodMillis)
        val reservation = matchQueue.reserve(
            mode,
            actorId,
            "${settings.mode}-room-$roomSeq",
            settings.requiredPlayers,
        )
        return BingoRoomAllocation(reservation.roomId, settings)
    }

    @Synchronized
    private fun nextRoomSeq(): Int {
        roomSeq += 1
        return roomSeq
    }
}
