package systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.sessions.handlers

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.customergateway.CustomerActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes

class SubscribeDeliverySessionHandler(
    private val channels: ZLinkClient,
    private val customers: CustomerActorDirectory,
) : ZLinkTypedSessionPacketHandler<ZLinkSessionContext, SubscribeDeliveryReq> {
    override fun packetName(): String = "SubscribeDeliveryReq"

    override fun messageType(): Class<SubscribeDeliveryReq> = SubscribeDeliveryReq::class.java

    override fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        message: SubscribeDeliveryReq,
    ) {
        val ensured = channels
            .requestToChannel(SampleNames.CustomerRouteChannel, EnsureCustomerActorReq(CustomerId))
            .await(EnsureCustomerActorRes::class.java)
        if (context.actors().find(ensured.actor.actorId()).isEmpty) {
            context.actors().bind(
                ensured.actor.toActorRef(),
            ).let { ZLinkAwait.await(it) }
        }
        customers.subscribe(ensured.customerId, message.deliveryId)
        context.client()
            .reply(SubscribeDeliveryRes(message.deliveryId))
            .submit()
    }

    private companion object {
        const val CustomerId = "customer-1"
    }
}
