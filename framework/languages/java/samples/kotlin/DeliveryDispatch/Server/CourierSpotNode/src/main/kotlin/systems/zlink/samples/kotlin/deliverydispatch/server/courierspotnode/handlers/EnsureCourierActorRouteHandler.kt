package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.spots.ZLinkSpotRequestHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ActorRefWire
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq

class EnsureCourierActorRouteHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSpotRequestHandler<CourierEntrySpot, EnsureCourierActorReq, EnsureCourierActorRes> {
    override fun handle(
        spot: CourierEntrySpot,
        request: EnsureCourierActorReq,
    ): EnsureCourierActorRes {
        val actor = ZLinkAwait.await(actors.getOrCreate(request.courierId, SampleNames.CourierActorType, request))
        return EnsureCourierActorRes(
            courierId = request.courierId,
            actor = ActorRefWire.from(actor),
        )
    }
}
