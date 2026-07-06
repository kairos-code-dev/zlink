package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.actors.ZLinkActorRefSnapshot
import systems.zlink.framework.spots.ZLinkSpotRequestHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorRes

class FindCourierActorRouteHandler(
    private val actors: ZLinkActorManager,
) : ZLinkSpotRequestHandler<CourierEntrySpot, FindCourierActorReq, FindCourierActorRes> {
    override fun handle(
        spot: CourierEntrySpot,
        request: FindCourierActorReq,
    ): FindCourierActorRes {
        val actor = ZLinkAwait.await(actors.find(request.courierId))
        return FindCourierActorRes(
            courierId = request.courierId,
            actor = actor.map { ZLinkActorRefSnapshot.from(it) }.orElse(null),
        )
    }
}
