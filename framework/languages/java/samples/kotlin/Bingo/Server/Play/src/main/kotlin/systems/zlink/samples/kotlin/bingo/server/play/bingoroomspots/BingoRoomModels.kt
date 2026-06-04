package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
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
    val recipient: PlayerActor,
    val state: BingoRoomState,
    val joinedActorId: String?,
    val joinedDisplayName: String?,
    val seat: Int,
    val host: Boolean,
    val drawnNumber: Int,
)

data class BingoRoomPlayer(
    val actor: PlayerActor,
    val seat: Int,
    val card: BingoCard,
) {
    fun toState(hostActorId: String): BingoPlayerState =
        BingoPlayerState(
            actor.actorId(),
            actor.displayName,
            seat,
            actor.actorId() == hostActorId,
            card.numbersSnapshot(),
            card.marksSnapshot(),
            card.completedLines(),
        )
}
