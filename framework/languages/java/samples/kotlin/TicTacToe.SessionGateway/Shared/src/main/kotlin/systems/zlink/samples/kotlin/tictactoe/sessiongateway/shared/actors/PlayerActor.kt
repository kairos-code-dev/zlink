package systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext

class PlayerActor(
    private val actorId: String,
    private val context: ZLinkActorContext,
) : ZLinkActor {
    override fun actorId(): String = actorId
    override fun context(): ZLinkActorContext = context
}
