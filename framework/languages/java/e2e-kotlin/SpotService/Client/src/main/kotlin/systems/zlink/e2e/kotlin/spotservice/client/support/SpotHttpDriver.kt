package systems.zlink.e2e.kotlin.spotservice.client.support

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import java.time.Duration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.locations.SpotRef
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal class SpotHttpDriver(
    private val playA: String = Env.get("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT"),
    private val playB: String = Env.get("ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT"),
    private val outbound: ZLinkSpotOutbound? = null,
    private val routes: ZLinkRouteClient? = null
) {
    fun requestState(
        spotRid: String,
        op: String,
        timeoutMilliseconds: Int = 5_000,
        packetName: String = "StateReq"
    ): Contracts.StateRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            return currentOutbound.requestToSpot(target, Contracts.StateReq(op))
                .packetName(packetName)
                .timeout(Duration.ofMillis(timeoutMilliseconds.coerceIn(1, 30_000).toLong()))
                .await(Contracts.StateRes::class.java)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/state/request",
            Contracts.SpotStateRouteReq(spotRid, op, timeoutMilliseconds, packetName),
            Contracts.StateRes::class.java
        )
    }

    fun sendState(
        spotRid: String,
        value: String,
        packetName: String = "StateMsg"
    ): Contracts.AckRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            currentOutbound.sendToSpot(target, Contracts.StateMsg(value))
                .packetName(packetName)
                .await()
            return Contracts.AckRes(true)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/state/command",
            Contracts.SpotStateCommandReq(spotRid, value, packetName),
            Contracts.AckRes::class.java
        )
    }

    fun requestStage(
        spotRid: String,
        marker: String,
        op: String
    ): Contracts.StateRes =
        postJson(
            endpointFor(spotRid),
            "/spot/stage/request",
            Contracts.SpotStageProbeReq(spotRid, marker, op),
            Contracts.StateRes::class.java
        )

    fun startStageTimer(
        spotRid: String,
        name: String,
        periodMilliseconds: Int
    ): Contracts.SpotStageTimerRes =
        postJson(
            endpointFor(spotRid),
            "/spot/stage/timer",
            Contracts.SpotStageTimerReq(spotRid, name, periodMilliseconds),
            Contracts.SpotStageTimerRes::class.java
        )

    fun requestSlow(
        spotRid: String,
        value: String,
        timeoutMilliseconds: Int
    ): Contracts.StateRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            return currentOutbound.requestToSpot(target, Contracts.SlowReq(value))
                .timeout(Duration.ofMillis(timeoutMilliseconds.coerceIn(1, 30_000).toLong()))
                .await(Contracts.StateRes::class.java)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/slow/request",
            Contracts.SpotSlowRouteReq(spotRid, value, timeoutMilliseconds),
            Contracts.StateRes::class.java
        )
    }

    fun requestOutbound(spotRid: String, value: String): Contracts.OutboundRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            return currentOutbound.requestToSpot(target, Contracts.OutboundReq(value))
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.OutboundRes::class.java)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/outbound/request",
            Contracts.SpotOutboundRouteReq(spotRid, value),
            Contracts.OutboundRes::class.java
        )
    }

    fun sendOutbound(
        spotRid: String,
        value: String,
        packetName: String = "OutboundMsg"
    ): Contracts.AckRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            currentOutbound.sendToSpot(
                spotRef("room-a"),
                Contracts.SpotToSpotCommandReq(spotRid, value)
            )
                .packetName("SpotToSpotCommandReq")
                .await()
            return Contracts.AckRes(true)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/outbound/command",
            Contracts.SpotOutboundCommandReq(spotRid, value, packetName),
            Contracts.AckRes::class.java
        )
    }

    fun routePing(targetRid: String, value: String): Contracts.RoutePingRes {
        val currentRoutes = routes
        if (currentRoutes != null) {
            return currentRoutes.requestToNode(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from(targetRid),
                Contracts.RoutePingReq(value)
            )
                .packetName(Contracts.ROUTE_PACKET)
                .timeout(Duration.ofSeconds(5))
                .await(Contracts.RoutePingRes::class.java)
        }
        return postJson(
            playA,
            "/route/ping",
            Contracts.RoutePingHttpReq(targetRid, value),
            Contracts.RoutePingRes::class.java
        )
    }

    private fun endpointFor(spotRid: String): String =
        if (spotRid == "room-b") playB else playA

    private fun spotRef(spotRid: String): SpotRef =
        SpotRef(Contracts.SPOT_MESH, targetNode(spotRid), RoutingId.from(spotRid))

    private fun targetNode(spotRid: String): RoutingId =
        RoutingId.from(if (spotRid == "room-b") "play-b" else "play-a")
}
