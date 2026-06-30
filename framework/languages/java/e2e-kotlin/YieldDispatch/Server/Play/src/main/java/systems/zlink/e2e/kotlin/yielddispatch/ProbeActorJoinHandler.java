package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class ProbeActorJoinHandler {
    @ZLinkSpotActorRequest(packetName = "ActorJoinReq")
    public Contracts.ActorJoinRes handle(
        ProbeSpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ActorJoinReq request,
        CancellationToken cancellationToken) {
        if (request.millis() > 0) {
            spot.context().outbound()
                .requestToChannel(Contracts.DELAY_CHANNEL, new Contracts.DelayReq(request.value(), request.millis()))
                .timeout(Duration.ofSeconds(5))
                .yield(Contracts.DelayRes.class);
        }
        return actor.context()
            .joinSpot(RoutingId.from(request.spotRid()), request)
            .yield(Contracts.ActorJoinRes.class)
            .reply();
    }
}
