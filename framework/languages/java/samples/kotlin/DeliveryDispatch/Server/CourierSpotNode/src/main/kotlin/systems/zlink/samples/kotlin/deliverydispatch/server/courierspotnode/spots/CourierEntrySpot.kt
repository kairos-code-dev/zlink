package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots

import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.ActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.CourierActor

class CourierEntrySpot(
    private val entryContext: ZLinkEntrySpotContext,
    private val actors: ActorDirectory,
) : ZLinkSuspendingEntrySpot<CourierActor>() {
    override fun context(): ZLinkEntrySpotContext = entryContext

    override suspend fun onCreateActorSuspending(
        actor: CourierActor,
        createRequest: ZLinkMessage,
    ) {
        actors.register(actor)
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse = ZLinkSpotActorJoinResponse.accept()

    override suspend fun onJoinedActorSuspending(actor: CourierActor) {
        actors.register(actor)
    }

    override suspend fun onLeaveActorSuspending(actor: CourierActor) {
        actors.remove(actor.actorId())
    }
}
