package systems.zlink.samples.kotlin.bingo.server.play.domain.bingo

import systems.zlink.samples.kotlin.bingo.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoPlayerState
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomState

enum class BingoRoomEventKind {
    PLAYER_JOINED,
    GAME_STARTED,
    NUMBER_DRAWN,
    STATE,
    GAME_ENDED,
}

data class BingoRoomEvent(
    val kind: BingoRoomEventKind,
    val recipientActorId: String,
    val state: BingoRoomState,
    val joinedActorId: String?,
    val joinedDisplayName: String?,
    val seat: Int,
    val host: Boolean,
    val drawnNumber: Int,
)

data class BingoRoomPlayer(
    val actorId: String,
    val displayName: String,
    val seat: Int,
    var card: BingoCard?,
) {
    fun toState(hostActorId: String): BingoPlayerState =
        BingoPlayerState(
            actorId,
            displayName,
            seat,
            actorId == hostActorId,
            card?.numbersSnapshot() ?: emptyList(),
            card?.marksSnapshot() ?: emptyList(),
            card?.completedLines() ?: 0,
        )
}

data class BingoRoomSettings(
    val roomName: String,
    val mode: String,
    val requiredPlayers: Int,
    val maxDrawNumber: Int,
    val drawPeriodMillis: Long,
) {
    companion object {
        fun create(
            mode: String,
            roomSeq: Int,
        ): BingoRoomSettings {
            check(mode == "two-player") { "Unsupported bingo mode. mode=$mode" }
            return BingoRoomSettings(
                roomName = "Bingo Room %03d".format(roomSeq),
                mode = mode,
                requiredPlayers = 2,
                maxDrawNumber = 15,
                drawPeriodMillis = SampleTimings.DrawPeriod.toMillis(),
            )
        }
    }
}
