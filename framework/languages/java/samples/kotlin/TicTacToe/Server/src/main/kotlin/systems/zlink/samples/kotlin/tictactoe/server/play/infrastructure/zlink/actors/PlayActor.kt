package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerInfo

class PlayActor(
    val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    private var joinedRoomId: String? = null
    private var player: PlayerInfo? = null
    var destroyAfterEntrySpotJoin: Boolean = false
        private set
    var disconnected: Boolean = false
        private set

    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun applyPlayer(player: PlayerInfo) {
        require(player.actorId == actorId) { "player actor id does not match actor" }
        this.player = player
    }

    fun requirePlayer(): PlayerInfo =
        player ?: throw IllegalStateException("actor has not been authenticated")

    fun playerOrNull(): PlayerInfo? = player

    fun incrementWins(): Int {
        val current = requirePlayer()
        val updated = current.copy(wins = current.wins + 1)
        player = updated
        return updated.wins
    }

    fun joinGame(roomId: String) {
        joinedRoomId = roomId
    }

    fun requireJoinedGame(): String =
        joinedRoomId ?: throw IllegalStateException("actor has not joined a game")

    fun joinedRoomIdOrNull(): String? = joinedRoomId

    fun markForDestroyAfterRoomLeave() {
        destroyAfterEntrySpotJoin = true
    }

    fun markDisconnected() {
        disconnected = true
    }
}
