package systems.zlink.samples.kotlin.tictactoe.server.play.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class PlayActor(
    val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    var gameId: String = ""
        private set

    override fun actorId(): String = actorId

    override fun context(): ZLinkActorContext = context

    fun joinGame(gameId: String) {
        this.gameId = gameId
    }
}
