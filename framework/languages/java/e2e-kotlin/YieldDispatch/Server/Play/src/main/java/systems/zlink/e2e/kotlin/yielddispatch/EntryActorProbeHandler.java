package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;

public final class EntryActorProbeHandler {
    @ZLinkSpotActorRequest(packetName = "ProbeReq")
    public Contracts.ProbeRes handle(
        ProbeEntrySpot spot,
        ProbeActor actor,
        ZLinkSpotActorRequestContext context,
        Contracts.ProbeReq request,
        CancellationToken cancellationToken) {
        if (request.millis() <= 0) {
            return reply(spot, request, "immediate:" + request.op());
        }
        Contracts.DelayRes delayed = spot.context().outbound()
            .requestToChannel(Contracts.DELAY_CHANNEL, new Contracts.DelayReq(request.op(), request.millis()))
            .timeout(Duration.ofSeconds(5))
            .await(Contracts.DelayRes.class);
        return reply(spot, request, delayed.value());
    }

    private Contracts.ProbeRes reply(
        ProbeEntrySpot spot,
        Contracts.ProbeReq request,
        String value) {
        return new Contracts.ProbeRes(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            request.op(),
            value + "#entry");
    }
}
