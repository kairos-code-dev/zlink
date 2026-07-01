package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots.handlers.SubscribeDeliveryActorHandler
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes

class CustomerEntrySpot(
    private val entryContext: ZLinkEntrySpotContext,
    private val customers: CustomerActorDirectory,
) : ZLinkEntrySpot<CustomerActor> {
    override fun context(): ZLinkEntrySpotContext = entryContext

    override fun configure() {
        entryContext.handlers().addHandler(SubscribeDeliveryActorHandler::class.java)
    }

    override fun onCreateActor(
        actor: CustomerActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken,
    ) {
        customers.register(actor)
    }

    override fun onActorJoin(
        actor: CustomerActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse = ZLinkSpotActorJoinResponse.accept()

    fun subscribe(
        actor: CustomerActor,
        request: SubscribeDeliveryReq,
    ): SubscribeDeliveryRes {
        customers.subscribe(actor.actorId(), request.deliveryId)
        return SubscribeDeliveryRes(request.deliveryId)
    }
}
