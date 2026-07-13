package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler
import systems.zlink.framework.actors.ActorRef
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.framework.actors.ActorRefSnapshot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq

class EnsureCourierActorRouteHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSuspendingSpotRequestHandler<CourierEntrySpot, EnsureCourierActorReq, EnsureCourierActorRes> {
    override suspend fun handle(
        spot: CourierEntrySpot,
        request: EnsureCourierActorReq,
    ): EnsureCourierActorRes {
        val actor = actors.getOrCreate(request.courierId, SampleNames.CourierActorType, request).await()
        return EnsureCourierActorRes(
            courierId = request.courierId,
            actor = ActorRefSnapshot.from(actor),
        )
    }
}
