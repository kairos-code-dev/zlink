package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkCoroutineActorFactory
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime

class SupportUserActorFactory(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineActorFactory(coroutines) {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        SupportUserActor(actorId, context)
}
