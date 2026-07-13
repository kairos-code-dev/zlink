package systems.zlink.e2e.kotlin.spotservice.client.support

import systems.zlink.framework.kotlin.*

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import java.time.Duration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.spots.SpotHandle
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal class SpotHttpDriver(
    private val playA: String = Env.get("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT"),
    private val playB: String = Env.get("ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT"),
    private val outbound: ZLinkSpotOutbound? = null,
    private val routes: ZLinkRouteClient? = null,
    private val spotHandles: SpotHandleResolver? = null,
) {
    suspend fun requestState(
        spotRid: String,
        op: String,
        timeoutMilliseconds: Int = 5_000,
        packetName: String = "StateReq"
    ): Contracts.StateRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            return currentOutbound.requestToSpot(target, Contracts.StateReq(op))
                .timeout(Duration.ofMillis(timeoutMilliseconds.coerceIn(1, 30_000).toLong()))
                .awaitReply<Contracts.StateRes>()
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/state/request",
            Contracts.SpotStateRouteReq(spotRid, op, timeoutMilliseconds, packetName),
            Contracts.StateRes::class.java
        )
    }

    suspend fun sendState(
        spotRid: String,
        value: String,
        packetName: String = "StateMsg"
    ): Contracts.AckRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            currentOutbound.sendToSpot(target, Contracts.StateMsg(value)).submit()
            return Contracts.AckRes(true)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/state/command",
            Contracts.SpotStateCommandReq(spotRid, value, packetName),
            Contracts.AckRes::class.java
        )
    }

    suspend fun requestStage(
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

    suspend fun startStageTimer(
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

    suspend fun requestSlow(
        spotRid: String,
        value: String,
        timeoutMilliseconds: Int
    ): Contracts.StateRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            return currentOutbound.requestToSpot(target, Contracts.SlowReq(value))
                .timeout(Duration.ofMillis(timeoutMilliseconds.coerceIn(1, 30_000).toLong()))
                .awaitReply<Contracts.StateRes>()
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/slow/request",
            Contracts.SpotSlowRouteReq(spotRid, value, timeoutMilliseconds),
            Contracts.StateRes::class.java
        )
    }

    suspend fun requestOutbound(spotRid: String, value: String): Contracts.OutboundRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            val target = spotRef(spotRid)
            return currentOutbound.requestToSpot(target, Contracts.OutboundReq(value))
                .timeout(Duration.ofSeconds(5))
                .awaitReply<Contracts.OutboundRes>()
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/outbound/request",
            Contracts.SpotOutboundRouteReq(spotRid, value),
            Contracts.OutboundRes::class.java
        )
    }

    suspend fun sendOutbound(
        spotRid: String,
        value: String,
        packetName: String = "OutboundMsg"
    ): Contracts.AckRes {
        val currentOutbound = outbound
        if (currentOutbound != null) {
            currentOutbound.sendToSpot(
                spotRef("room-a"),
                Contracts.SpotToSpotCommandReq(spotRid, value)
            ).submit()
            return Contracts.AckRes(true)
        }
        return postJson(
            endpointFor(spotRid),
            "/spot/outbound/command",
            Contracts.SpotOutboundCommandReq(spotRid, value, packetName),
            Contracts.AckRes::class.java
        )
    }

    suspend fun routePing(targetRid: String, value: String): Contracts.RoutePingRes {
        val currentRoutes = routes
        if (currentRoutes != null) {
            return currentRoutes.requestToNode(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from(targetRid),
                Contracts.RoutePingReq(value)
            )
                .timeout(Duration.ofSeconds(5))
                .awaitReply<Contracts.RoutePingRes>()
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

    private suspend fun spotRef(spotRid: String): SpotHandle =
        requireNotNull(spotHandles) { "SpotHandleResolver is required for framework routing" }
            .resolveSpotHandle(RoutingId.from(spotRid))
            .await()
            .orElseThrow { IllegalStateException("Spot location was not found: $spotRid") }
}
