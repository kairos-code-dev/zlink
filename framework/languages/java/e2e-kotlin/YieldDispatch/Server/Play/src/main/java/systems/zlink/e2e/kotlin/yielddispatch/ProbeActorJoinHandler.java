package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class ProbeActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinRequest")
    public Contracts.ActorJoinReply handle(
        ProbeSpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinRequest request,
        CancellationToken cancellationToken) {
        if (request.millis() > 0) {
            spot.context().outbound()
                .requestToChannel(Contracts.DELAY_CHANNEL, new Contracts.DelayRequest(request.value(), request.millis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayReply.class);
        }
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .yield(Contracts.ActorJoinReply.class)
            .reply();
    }
}
