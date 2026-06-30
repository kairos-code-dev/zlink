package systems.zlink.e2e.spotservice.shared;

import java.time.Duration;
import systems.zlink.framework.handlers.ZLinkSpotRequest;

public final class OutboundRequestHandler {
    @ZLinkSpotRequest
    public Contracts.OutboundReply handle(
        UserSpot spot,
        Contracts.OutboundRequest request) {
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
                new Contracts.OutboundCommand("send:" + request.value()))
            .packetName("OutboundCommand")
            .await();
        spot.context()
            .outbound()
            .publish("spot.events", new Contracts.MeshEvent("publish:" + request.value()))
            .packetName("MeshEvent")
            .await();
        spot.record("SpotOutbound", request.value() + "/" + channelReply);
        return new Contracts.OutboundReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            channelReply);
    }
}
