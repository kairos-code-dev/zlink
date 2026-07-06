package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.CourierActor
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionRes

class BindCourierSessionActorHandler : ZLinkEntrySpotActorRequestHandler<
    CourierEntrySpot,
    CourierActor,
    BindCourierSessionReq,
    BindCourierSessionRes,
    > {
    override fun handle(
        entrySpot: CourierEntrySpot,
        actor: CourierActor,
        context: ZLinkSpotActorRequestContext,
        request: BindCourierSessionReq,
        cancellationToken: CancellationToken,
    ): BindCourierSessionRes =
        BindCourierSessionRes(
            courierId = request.courierId,
            nodeRid = requireNotNull(request.actor) { "BindCourierSessionReq.actor is required" }.nodeRid().toString(),
            sessionRoute = requireNotNull(request.sessionRoute) { "BindCourierSessionReq.sessionRoute is required" },
        )
}
