package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler
import systems.zlink.framework.spots.ZLinkSpotActorSendContext
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots.CustomerEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusUpdatedMsg

class DeliveryStatusUpdatedHandler : ZLinkEntrySpotActorSendHandler<
    CustomerEntrySpot,
    CustomerActor,
    DeliveryStatusUpdatedMsg,
    > {
    override fun handle(
        entrySpot: CustomerEntrySpot,
        actor: CustomerActor,
        context: ZLinkSpotActorSendContext,
        message: DeliveryStatusUpdatedMsg,
        cancellationToken: CancellationToken,
    ) {
        actor.push(
            DeliveryStatusNotify(
                deliveryId = message.deliveryId,
                status = message.status,
                courierId = message.courierId,
                occurredAt = message.occurredAt,
            ),
        )
    }
}
