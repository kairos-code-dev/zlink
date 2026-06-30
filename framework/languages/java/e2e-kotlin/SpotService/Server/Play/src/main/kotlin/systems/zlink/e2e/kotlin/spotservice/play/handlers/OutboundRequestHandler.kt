package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import java.time.Duration
import systems.zlink.framework.handlers.ZLinkSpotRequest

class OutboundRequestHandler {
    @ZLinkSpotRequest
    fun handle(
        spot: UserSpot,
        request: Contracts.OutboundReq,
    ): Contracts.OutboundRes {
        val channelReply = spot.context()
            .outbound()
            .requestToChannel(Contracts.INGRESS_CHANNEL, request.value)
            .packetName("Noop")
            .timeout(Duration.ofSeconds(5))
            .await(String::class.java)
        spot.context()
            .outbound()
            .sendToChannel(
                Contracts.INGRESS_CHANNEL,
                Contracts.OutboundMsg("send:${request.value}"),
            )
            .packetName("OutboundMsg")
            .await()
        spot.context()
            .outbound()
            .publish("spot.events", Contracts.MeshMsg("publish:${request.value}"))
            .packetName("MeshMsg")
            .await()
        spot.record("SpotOutbound", "${request.value}/$channelReply")
        return Contracts.OutboundRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            channelReply,
        )
    }
}
