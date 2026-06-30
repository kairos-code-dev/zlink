package systems.zlink.e2e.spotservice.shared;

import java.time.Duration;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class OutboundReqHandler {
    @ZLinkSpotRequest
    public Contracts.OutboundRes handle(
        UserSpot spot,
        Contracts.OutboundReq request) {
        String channelReply = spot.context()
            .outbound()
            .requestToChannel(Contracts.INGRESS_CHANNEL, request.value())
            .packetName("Noop")
            .timeout(Duration.ofSeconds(5))
            .await(String.class);
        spot.context()
            .outbound()
            .sendToChannel(
                Contracts.INGRESS_CHANNEL,
                new Contracts.OutboundMsg("send:" + request.value()))
            .packetName("OutboundMsg")
            .await();
        spot.context()
            .outbound()
            .publish("spot.events", new Contracts.MeshMsg("publish:" + request.value()))
            .packetName("MeshMsg")
            .await();
        spot.record("SpotOutbound", request.value() + "/" + channelReply);
        return new Contracts.OutboundRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            channelReply);
    }
}
