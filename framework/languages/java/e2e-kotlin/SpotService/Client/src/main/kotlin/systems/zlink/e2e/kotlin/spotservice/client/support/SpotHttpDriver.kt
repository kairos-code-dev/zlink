package systems.zlink.e2e.kotlin.spotservice.client.support

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env

internal class SpotHttpDriver(
    private val playA: String = Env.get("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT"),
    private val playB: String = Env.get("ZLINK_KOTLIN_E2E_HTTP_B_ENDPOINT")
) {
    fun requestState(
        spotRid: String,
        op: String,
        timeoutMilliseconds: Int = 5_000,
        packetName: String = "StateReq"
    ): Contracts.StateRes =
        postJson(
            endpointFor(spotRid),
            "/spot/state/request",
            Contracts.SpotStateRouteReq(spotRid, op, timeoutMilliseconds, packetName),
            Contracts.StateRes::class.java
        )

    fun sendState(
        spotRid: String,
        value: String,
        packetName: String = "StateMsg"
    ): Contracts.AckRes =
        postJson(
            endpointFor(spotRid),
            "/spot/state/command",
            Contracts.SpotStateCommandReq(spotRid, value, packetName),
            Contracts.AckRes::class.java
        )

    fun requestSlow(
        spotRid: String,
        value: String,
        timeoutMilliseconds: Int
    ): Contracts.StateRes =
        postJson(
            endpointFor(spotRid),
            "/spot/slow/request",
            Contracts.SpotSlowRouteReq(spotRid, value, timeoutMilliseconds),
            Contracts.StateRes::class.java
        )

    fun requestOutbound(spotRid: String, value: String): Contracts.OutboundRes =
        postJson(
            endpointFor(spotRid),
            "/spot/outbound/request",
            Contracts.SpotOutboundRouteReq(spotRid, value),
            Contracts.OutboundRes::class.java
        )

    fun sendOutbound(
        spotRid: String,
        value: String,
        packetName: String = "OutboundMsg"
    ): Contracts.AckRes =
        postJson(
            endpointFor(spotRid),
            "/spot/outbound/command",
            Contracts.SpotOutboundCommandReq(spotRid, value, packetName),
            Contracts.AckRes::class.java
        )

    fun routePing(targetRid: String, value: String): Contracts.RoutePingRes =
        postJson(
            playA,
            "/route/ping",
            Contracts.RoutePingHttpReq(targetRid, value),
            Contracts.RoutePingRes::class.java
        )

    private fun endpointFor(spotRid: String): String =
        if (spotRid == "room-b") playB else playA
}
