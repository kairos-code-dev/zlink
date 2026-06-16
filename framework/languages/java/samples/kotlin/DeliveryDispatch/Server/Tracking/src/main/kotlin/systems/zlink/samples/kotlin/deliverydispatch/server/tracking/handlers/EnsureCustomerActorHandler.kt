package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ActorRefSnapshot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CustomerActorEnsured
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCustomerActor

@ZLinkHandlerGroup("tracking")
class EnsureCustomerActorHandler(
    private val actors: ZLinkActorManager,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<EnsureCustomerActor, CustomerActorEnsured> {
    override fun handle(
        request: EnsureCustomerActor,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val actor = actors.getOrCreate(request.customerId, SampleNames.CustomerActorType).await()
        val joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.TrackingSpotNodeRid))
            .timeout(SampleTimings.RequestTimeout)
            .submit()
            .await()
        CustomerActorEnsured(
            request.customerId,
            ActorRefSnapshot(joined.nodeRid().toBytes(), joined.actorId(), joined.epoch()),
        )
    }
}
