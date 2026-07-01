package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots.CustomerEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes

class SubscribeDeliveryActorHandler : ZLinkEntrySpotActorRequestHandler<
    CustomerEntrySpot,
    CustomerActor,
    SubscribeDeliveryReq,
    SubscribeDeliveryRes,
    > {
    override fun handle(
        entrySpot: CustomerEntrySpot,
        actor: CustomerActor,
        context: ZLinkSpotActorRequestContext,
        request: SubscribeDeliveryReq,
        cancellationToken: CancellationToken,
    ): SubscribeDeliveryRes =
        entrySpot.subscribe(actor, request)
}
