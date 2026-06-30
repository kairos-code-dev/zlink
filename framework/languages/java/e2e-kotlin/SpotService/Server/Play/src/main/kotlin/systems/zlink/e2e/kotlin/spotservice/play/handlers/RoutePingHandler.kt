package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.channels.ZLinkRouteRequestContext
import systems.zlink.framework.channels.ZLinkRouteRequestHandler

class RoutePingHandler(
    private val state: ScenarioState,
) : ZLinkRouteRequestHandler<Contracts.RoutePing, Contracts.RoutePong> {
    override fun handle(
        request: Contracts.RoutePing,
        context: ZLinkRouteRequestContext,
    ): Contracts.RoutePong {
        state.record("RoutePing", context.routingId().toString(), request.value)
        return Contracts.RoutePong(
            "route:${request.value}",
            state.nodeRid(),
            context.routingId().toString(),
        )
    }
}
