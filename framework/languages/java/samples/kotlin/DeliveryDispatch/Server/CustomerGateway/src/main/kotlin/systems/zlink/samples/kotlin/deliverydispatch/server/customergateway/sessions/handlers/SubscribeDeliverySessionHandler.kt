package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.sessions.handlers

import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionMessageContext
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCustomerActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes

class SubscribeDeliverySessionHandler(
    private val actors: ZLinkActorManager,
    private val customers: CustomerActorDirectory,
) : ZLinkSuspendingTypedSessionPacketHandler<ZLinkSessionContext, SubscribeDeliveryReq> {
    override fun packetName(): String = "SubscribeDeliveryReq"

    override fun messageType(): Class<SubscribeDeliveryReq> = SubscribeDeliveryReq::class.java

    override suspend fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionMessageContext,
        message: SubscribeDeliveryReq,
    ) {
        val find = FindCustomerActorReq(CustomerId)
        val actor = actors.find(find.customerId).await().orElse(null)
            ?: actors.getOrCreate(
                CustomerId,
                SampleNames.CustomerActorType,
                EnsureCustomerActorReq(CustomerId),
            ).await()
        if (context.actors().find(actor.actorId()).isEmpty) {
            context.actors().bind(actor).await()
        }
        customers.subscribe(CustomerId, message.deliveryId)
        context.client()
            .reply(SubscribeDeliveryRes(message.deliveryId))
            .submit()
    }

    private companion object {
        const val CustomerId = "customer-1"
    }
}
