package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class PlayerActor(
    private val actorId: String,
) : ZLinkActor {
    private val context = RecordingActorContext()

    override fun actorId(): String = actorId
    override fun context(): ZLinkActorContext = context
    fun pushes(): List<String> = context.pushes()
}
