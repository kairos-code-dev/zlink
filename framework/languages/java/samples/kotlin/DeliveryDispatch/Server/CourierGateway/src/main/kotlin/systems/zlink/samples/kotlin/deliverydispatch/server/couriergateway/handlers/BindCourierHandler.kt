package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.SpotHandle
import systems.zlink.framework.spots.SpotHandleResolver
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
    private val spots: SpotHandleResolver,
) : ZLinkSuspendingRequestHandler<BindCourierReq, BindCourierRes> {
    override suspend fun handle(
        request: BindCourierReq,
        context: ZLinkRequestContext,
    ): BindCourierRes {
        val placement = directory.choosePlacement(request.courierId)
        val address = address(placement)
        val found = routes
            .requestToSpot(address, FindCourierActorReq(request.courierId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(FindCourierActorRes::class.java)
            .await()
        val ensured = found.actor?.let { EnsureCourierActorRes(request.courierId, it) }
            ?: routes
                .requestToSpot(address, EnsureCourierActorReq(request.courierId))
                .timeout(SampleTimings.RequestTimeout)
                .submit(EnsureCourierActorRes::class.java)
                .await()
        val binding = directory.remember(ensured, request.sessionRoute)
        return BindCourierRes(request.courierId, binding.actor, binding.sessionRoute)
    }

    private suspend fun address(placement: String): SpotHandle {
        val nodeRid = RoutingId.from(placement)
        return spots.resolveSpotHandle(nodeRid).await()
            .orElseThrow { IllegalStateException("spot not found: $nodeRid") }
    }
}
