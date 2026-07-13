package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.CourierActor
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CourierDecisionMsg

class CourierDecisionActorHandler : ZLinkSuspendingEntrySpotActorSendHandler<
    CourierEntrySpot,
    CourierActor,
    CourierDecisionMsg,
    > {
    override suspend fun handle(
        entrySpot: CourierEntrySpot,
        actor: CourierActor,
        context: ZLinkSpotActorSendContext,
        message: CourierDecisionMsg,
    ) {
        actor.complete(message)
    }
}
