package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.framework.channels.ZLinkRouteClient

internal object SmF3Scenario {
    fun run(routes: ZLinkRouteClient) {
        val routeReply = routes.requestTo(
            Contracts.ROUTE_CHANNEL,
            RoutingId.from("play-a"),
            Contracts.RoutePingReq("route-mesh-normal"),
        )
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.RoutePingRes::class.java)
        ensure(routeReply.nodeRid == "play-a", "SM-F3 route-channel target node mismatch")
        ensure(routeReply.routeRid == "client-route-mesh", "SM-F3 route-channel source routing id mismatch")
        ensure(routeReply.value == "route:route-mesh-normal", "SM-F3 route-channel reply mismatch")
        println("scenario SM-F3 passed")
    }
}
