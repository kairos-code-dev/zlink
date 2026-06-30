package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestHandler

class RoutePingHandler(
    private val state: ScenarioState,
) : ZLinkRouteRequestHandler<Contracts.RoutePingReq, Contracts.RoutePingRes> {
    override fun handle(
        request: Contracts.RoutePingReq,
        context: ZLinkRouteRequestContext,
    ): Contracts.RoutePingRes {
        state.record("RoutePingReq", context.routingId().toString(), request.value)
        return Contracts.RoutePingRes(
            "route:${request.value}",
            state.nodeRid(),
            context.routingId().toString(),
        )
    }
}
