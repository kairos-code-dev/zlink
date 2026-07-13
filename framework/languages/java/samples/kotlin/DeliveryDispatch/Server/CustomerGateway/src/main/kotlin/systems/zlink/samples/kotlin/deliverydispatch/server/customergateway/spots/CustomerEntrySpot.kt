package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.spots

import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpot
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes

class CustomerEntrySpot(
    private val entryContext: ZLinkEntrySpotContext,
    private val customers: CustomerActorDirectory,
) : ZLinkSuspendingEntrySpot<CustomerActor>() {
    override fun context(): ZLinkEntrySpotContext = entryContext

    override suspend fun onCreateActorSuspending(
        actor: CustomerActor,
        createRequest: ZLinkMessage,
    ) {
        customers.register(actor)
    }

    override suspend fun onActorJoinSuspending(
        actorId: String,
        request: ZLinkMessage,
    ): ZLinkSpotActorJoinResponse = ZLinkSpotActorJoinResponse.accept()

    override suspend fun onJoinedActorSuspending(actor: CustomerActor) {
        customers.register(actor)
    }

    override suspend fun onLeaveActorSuspending(actor: CustomerActor) {
        customers.remove(actor.actorId())
    }

    fun subscribe(
        actor: CustomerActor,
        request: SubscribeDeliveryReq,
    ): SubscribeDeliveryRes {
        customers.subscribe(actor.actorId(), request.deliveryId)
        return SubscribeDeliveryRes(request.deliveryId)
    }
}
