package systems.zlink.samples.kotlin.tictactoe.server.play.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class PlayActor(
    val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    private var joinedGameId: String? = null

    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun joinGame(gameId: String) {
        joinedGameId = gameId
    }

    fun requireJoinedGame(): String =
        joinedGameId ?: throw IllegalStateException("actor has not joined a game")
}
