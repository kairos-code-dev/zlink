package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.locations.SpotRef
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.CourierDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.FindCourierActorRes

@ZLinkHandlerGroup("courier-gateway")
class BindCourierHandler(
    private val directory: CourierDirectory,
    private val routes: ZLinkRouteClient,
) : ZLinkRequestHandler<BindCourierReq, BindCourierRes> {
    override fun handle(
        request: BindCourierReq,
        context: ZLinkRequestContext,
    ): BindCourierRes {
        val placement = directory.choosePlacement(request.courierId)
        val address = address(placement)
        val found = routes
            .requestToSpot(SampleNames.CourierSpotMesh, address, FindCourierActorReq(request.courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(FindCourierActorRes::class.java)
        val ensured = found.actor?.let { EnsureCourierActorRes(request.courierId, it) }
            ?: routes
                .requestToSpot(SampleNames.CourierSpotMesh, address, EnsureCourierActorReq(request.courierId))
                .timeout(SampleTimings.RequestTimeout)
                .await(EnsureCourierActorRes::class.java)
        val binding = directory.remember(ensured, request.sessionRoute)
        return BindCourierRes(request.courierId, binding.actor, binding.sessionRoute)
    }

    private fun address(placement: String): SpotRef {
        val nodeRid = RoutingId.from(placement)
        return SpotRef(SampleNames.CourierSpotMesh, nodeRid, nodeRid)
    }
}
