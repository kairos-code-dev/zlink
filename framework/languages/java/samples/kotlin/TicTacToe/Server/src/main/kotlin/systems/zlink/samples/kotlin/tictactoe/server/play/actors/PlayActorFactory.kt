package systems.zlink.samples.kotlin.tictactoe.server.play.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkCoroutineActorFactory
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime

class PlayActorFactory(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineActorFactory(coroutines) {
    override suspend fun create(
        actorId: String,
        context: ZLinkActorContext,
    ): ZLinkActor =
        PlayActor(actorId, context)
}
