package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class PlayActor(
    val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    private var joinedRoomId: String? = null

    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun joinGame(roomId: String) {
        joinedRoomId = roomId
    }

    fun requireJoinedGame(): String =
        joinedRoomId ?: throw IllegalStateException("actor has not joined a game")
}
