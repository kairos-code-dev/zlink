package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ActorRefSnapshot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CustomerActorEnsured
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActor

@ZLinkHandlerGroup("tracking")
class EnsureCustomerActorHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingRequestHandler<EnsureCustomerActor, CustomerActorEnsured> {
    override suspend fun handle(
        request: EnsureCustomerActor,
        context: ZLinkRequestContext,
    ) = run {
        val actor = actors.getOrCreate(request.customerId, SampleNames.CustomerActorType).await()
        val joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.TrackingSpotNodeRid), Message.from(ByteArray(0)))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Message::class.java)
            .await()
        CustomerActorEnsured(
            request.customerId,
            ActorRefSnapshot(joined.actor().nodeRid().toBytes(), joined.actor().actorId(), joined.actor().epoch()),
        )
    }
}
