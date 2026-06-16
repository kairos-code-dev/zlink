package systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorRef
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSessionPacketHandler
import systems.zlink.framework.streams.ZLinkSessionContext
import systems.zlink.framework.streams.ZLinkStreamCodec as FrameworkStreamCodec
import systems.zlink.framework.streams.ZLinkStreamHeader
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CustomerActorEnsured
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CustomerDeliverySubscribed
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActor
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeCustomerToDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryAccepted
import systems.zlink.samples.kotlin.deliverydispatch.server.session.sessions.CustomerSessionDirectory
import systems.zlink.stream.connector.ZLinkStreamCodec as ConnectorStreamCodec
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload
import systems.zlink.stream.connector.json.ZLinkStreamJson

class SubscribeDeliveryHandler(
    private val channels: ZLinkClient,
    private val sessions: CustomerSessionDirectory,
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSessionPacketHandler<ZLinkSessionContext>(coroutines, "SubscribeDelivery") {
    override suspend fun handleSuspending(
        context: ZLinkSessionContext,
        header: ZLinkStreamHeader,
        payload: Message,
    ) {
        val request = ZLinkStreamJson.decode(
            ZLinkStreamEncodedPayload(
                header.packetName(),
                payload,
                header.metadata(),
                connectorCodec(header),
            ),
            SubscribeDelivery::class.java,
        )
        require(request.deliveryId.isNotBlank()) { "SubscribeDelivery requires deliveryId." }

        val ensured = channels
            .requestToChannel(SampleNames.TrackingChannel, EnsureCustomerActor(CustomerId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(CustomerActorEnsured::class.java)
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
                SubscribeCustomerToDelivery(CustomerId, request.deliveryId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(CustomerDeliverySubscribed::class.java)
            .await()
        sessions.subscribe(context, subscribed.deliveryId)
        context.client()
            .reply(SubscribeDeliveryAccepted(subscribed.deliveryId))
            .submit()
            .await()
    }

    private fun connectorCodec(header: ZLinkStreamHeader): ConnectorStreamCodec =
        when (header.codec()) {
            FrameworkStreamCodec.RAW -> ConnectorStreamCodec.RAW
            FrameworkStreamCodec.JSON -> ConnectorStreamCodec.JSON
            FrameworkStreamCodec.MESSAGE_PACK -> ConnectorStreamCodec.MESSAGE_PACK
            FrameworkStreamCodec.PROTOBUF -> ConnectorStreamCodec.PROTOBUF
        }

    companion object {
        private const val CustomerId = "customer-1"
    }
}
