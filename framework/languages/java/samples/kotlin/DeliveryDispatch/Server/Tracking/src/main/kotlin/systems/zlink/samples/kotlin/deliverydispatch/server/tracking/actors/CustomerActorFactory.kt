package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.actors

import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.actors.ZLinkActorContext
import systems.zlink.framework.kotlin.ZLinkCoroutineActorFactory
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime

class CustomerActorFactory(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineActorFactory(coroutines) {
    override suspend fun createActor(actorId: String, context: ZLinkActorContext): ZLinkActor =
        CustomerActor(actorId, context)
}
