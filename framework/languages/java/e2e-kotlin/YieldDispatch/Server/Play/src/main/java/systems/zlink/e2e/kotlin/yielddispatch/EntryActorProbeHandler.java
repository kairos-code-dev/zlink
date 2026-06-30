package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorProbeHandler {
    @ZLinkSpotActorRequest(packetName = "ProbeRequest")
    public Contracts.ProbeReply handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ProbeRequest request,
        CancellationToken cancellationToken) {
        if (request.millis() <= 0) {
            return reply(spot, request, "immediate:" + request.op());
        }
        Contracts.DelayReply delayed = spot.context().outbound()
            .requestToChannel(Contracts.DELAY_CHANNEL, new Contracts.DelayRequest(request.op(), request.millis()))
            .timeout(Duration.ofSeconds(5))
            .yield(Contracts.DelayReply.class);
        return reply(spot, request, delayed.value());
    }

    private Contracts.ProbeReply reply(
        ProbeEntrySpot spot,
        Contracts.ProbeRequest request,
        String value) {
        return new Contracts.ProbeReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            request.op(),
            value + "#entry");
    }
}
