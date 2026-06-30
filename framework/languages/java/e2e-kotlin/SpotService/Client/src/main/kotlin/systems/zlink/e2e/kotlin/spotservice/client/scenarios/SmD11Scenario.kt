package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.framework.channels.ZLinkRouteClient

internal object SmD11Scenario {
    fun run(routes: ZLinkRouteClient) {
        val actorId = "actor-sm-d11-mixed"
        val profile = Contracts.ActorProfile("Mixed", 11, listOf("stream", "channel"))
        val connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            connector.connect().await()
            val auth = connector
                .request(Contracts.ActorAuthRequest(actorId, profile))
                .await(Contracts.ActorAuthReply::class.java)
            ensure(auth.actorId == actorId, "SM-D11 auth actor mismatch")

            val streamReply = connector
                .request(Contracts.ActorEchoRequest("stream-side", 11, profile))
                .await(Contracts.ActorEchoReply::class.java)
            ensure(streamReply.actorId == actorId, "SM-D11 stream request actor mismatch")
            ensure(streamReply.value == "entry:stream-side", "SM-D11 stream reply value mismatch")
        } finally {
            try {
                connector.close().await()
            } catch (_: Exception) {
            }
        }

        val routeReply = routes.requestTo(
            Contracts.ROUTE_CHANNEL,
            RoutingId.from("play-a"),
            Contracts.RoutePing("channel-side"),
        )
            .packetName(Contracts.ROUTE_PACKET)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.RoutePong::class.java)
        ensure(routeReply.nodeRid == "play-a", "SM-D11 channel request node mismatch")
        ensure(routeReply.value == "route:channel-side", "SM-D11 channel reply value mismatch")
        ensure(routeReply.routeRid == "client-actor-session", "SM-D11 channel source routing id mismatch")

        println("scenario SM-D11 passed")
    }
}
