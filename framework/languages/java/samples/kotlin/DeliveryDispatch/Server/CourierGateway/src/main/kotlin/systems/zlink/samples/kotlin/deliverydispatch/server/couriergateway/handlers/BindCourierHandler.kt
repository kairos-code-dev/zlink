package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.CourierDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.EnsureCourierActorReq

@ZLinkHandlerGroup("courier-gateway")
class BindCourierHandler(
    private val directory: CourierDirectory,
    private val routes: ZLinkRouteClient,
) : ZLinkRequestHandler<BindCourierReq, BindCourierRes> {
    override fun handle(
        request: BindCourierReq,
        context: ZLinkRequestContext,
    ): BindCourierRes {
        val ensured = await(
            routes
                .requestTo(
                    SampleNames.CourierActorNodeRouteChannel,
                    RoutingId.from(directory.choosePlacement(request.courierId)),
                    EnsureCourierActorReq(request.courierId),
                )
                .submit(EnsureCourierActorRes::class.java),
        )
        val binding = directory.remember(ensured, request.sessionRoute)
        return BindCourierRes(request.courierId, binding.actor, binding.sessionRoute)
    }
}
