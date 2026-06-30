package systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkSuspendingTypedSessionPacketHandler
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkSessionDispatchContext
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeCustomerToDeliveryRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeCustomerToDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.CustomerSessionDirectory

class SubscribeDeliveryHandler(
    private val channels: ZLinkClient,
    private val sessions: CustomerSessionDirectory,
) : ZLinkSuspendingTypedSessionPacketHandler<ZLinkSessionContext, SubscribeDeliveryReq> {
    override fun packetName(): String = "SubscribeDeliveryReq"

    override fun messageType(): Class<SubscribeDeliveryReq> = SubscribeDeliveryReq::class.java

    override suspend fun handle(
        context: ZLinkSessionContext,
        dispatch: ZLinkSessionDispatchContext,
        request: SubscribeDeliveryReq,
    ) {
        require(request.deliveryId.isNotBlank()) { "SubscribeDeliveryReq requires deliveryId." }

        val ensured = channels
            .requestToChannel(SampleNames.TrackingChannel, EnsureCustomerActorReq(CustomerId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(EnsureCustomerActorRes::class.java)
            .await()
        context.actors()
            .bind(
                ZLinkActorRef(
                    RoutingId.from(ensured.actor.nodeRid),
                    ensured.actor.actorId,
                    ensured.actor.generation,
                ),
            )
            .await()
        System.err.println("deliverydispatch session: bound customer actor=${ensured.actor.actorId}")

        val subscribed = channels
            .requestToChannel(
                SampleNames.TrackingChannel,
                SubscribeCustomerToDeliveryReq(CustomerId, request.deliveryId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(SubscribeCustomerToDeliveryRes::class.java)
            .await()
        sessions.subscribe(context, subscribed.deliveryId)
        context.client()
            .reply(SubscribeDeliveryRes(subscribed.deliveryId))
            .submit()
            .await()
    }

    companion object {
        private const val CustomerId = "customer-1"
    }
}
