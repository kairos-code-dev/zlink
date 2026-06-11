package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkCoroutineActorFactory
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime

class PlayerActorFactory(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineActorFactory(coroutines) {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        PlayerActor(actorId, context)
}
