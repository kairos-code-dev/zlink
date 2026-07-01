package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.CourierActor
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CourierDecisionMsg

class CourierDecisionActorHandler : ZLinkEntrySpotActorSendHandler<
    CourierEntrySpot,
    CourierActor,
    CourierDecisionMsg,
    > {
    override fun handle(
        entrySpot: CourierEntrySpot,
        actor: CourierActor,
        context: ZLinkSpotActorSendContext,
        message: CourierDecisionMsg,
        cancellationToken: CancellationToken,
    ) {
        actor.complete(message)
    }
}
